/*
 * Copyright (C) 2018 taylor.fish <contact@taylor.fish>
 *
 * This file is part of Midislide.
 *
 * Midislide is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Midislide is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Midislide.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "midislide.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

#pragma GCC diagnostic ignored "-Wunused-parameter"

#define PLUGIN_URI "https://taylor.fish/plugins/midislide"

/* Forward declarations */

static const LV2_Atom_Event *run_body(
    MidiSlide *plugin, uint32_t n_samples,
    const LV2_Atom_Event *start_event, uint32_t frames,
    uint32_t output_capacity);

static inline void handle_atom_object(
    MidiSlide *plugin, const LV2_Atom_Object *object);

static inline MidiAction get_midi_action(const uint8_t *message);

static inline bool handle_midi_message(
    MidiSlide *plugin, const uint8_t *message, MidiAction action,
    uint32_t output_capacity);

static inline void compact_stack(MidiSlide *plugin);

static inline void move_primary_to_stack_top(
    MidiSlide *plugin, uint8_t start_at);

static inline void set_bend(
    MidiSlide *plugin, int value, uint32_t frames,
    uint32_t output_capacity);

static inline bool set_bend_from_slide(
    MidiSlide *plugin, uint8_t key, uint8_t velocity, uint8_t base_key,
    uint32_t frames, uint32_t output_capacity);

static inline void set_bend_from_key(
    MidiSlide *plugin, uint8_t key, uint32_t frames,
    uint32_t output_capacity);

static inline int relative_key_to_bend(
    MidiSlide *plugin, double relative_key);

static inline void stop_note(
    MidiSlide *plugin, uint32_t frames, uint32_t output_capacity);

static inline void play_note(
    MidiSlide *plugin, uint8_t key, uint8_t velocity, uint32_t frames,
    uint32_t output_capacity);

static inline void send_midi_message(
    MidiSlide *plugin, MidiEvent *event, uint32_t output_capacity);

static inline void handle_note_on(
    MidiSlide *plugin, uint8_t key, uint8_t velocity);

static inline void handle_note_off(MidiSlide *plugin, uint8_t key);

static inline void handle_all_notes_off(MidiSlide *plugin);

static inline void add_to_stack(
    MidiSlide *plugin, uint8_t key, uint8_t velocity);

static inline void remove_from_stack(MidiSlide *plugin, uint8_t key);

static inline void clear_stack(MidiSlide *plugin);

/* End forward declarations */

static inline void map_uris(LV2_URID_Map *map, MidiSlideURIs *uris) {
    uris->midi_Event = map->map(map->handle, LV2_MIDI__MidiEvent);
    uris->atom_Float = map->map(map->handle, LV2_ATOM__Float);
    uris->time_Position = map->map(map->handle, LV2_TIME__Position);
    uris->time_beatsPerMinute = map->map(
        map->handle, LV2_TIME__beatsPerMinute
    );
    uris->atom_Object = map->map(map->handle, LV2_ATOM__Object);
    uris->atom_Blank = map->map(map->handle, LV2_ATOM__Blank);
    uris->atom_Resource = map->map(map->handle, LV2_ATOM__Resource);
}

static LV2_Handle instantiate(
        const LV2_Descriptor *descriptor, double rate, const char *bundle_path,
        const LV2_Feature * const *features) {
    LV2_URID_Map *map = NULL;
    for (size_t i = 0; features[i] != NULL; i++) {
        if (strcmp(features[i]->URI, LV2_URID_URI "#map") == 0) {
            map = (LV2_URID_Map *)features[i]->data;
        }
    }

    if (map == NULL) {
        fprintf(stderr, "Host does not support urid:map.\n");
        return NULL;
    }

    MidiSlide *plugin = calloc(1, sizeof(MidiSlide));
    if (plugin == NULL) {
        fprintf(stderr, "Not enough memory to allocate plugin.\n");
        return NULL;
    }

    plugin->map = map;
    plugin->sample_rate = rate;
    map_uris(map, &plugin->uris);
    return (LV2_Handle)plugin;
}

static void connect_port(LV2_Handle instance, uint32_t port, void *data) {
    MidiSlide *plugin = (MidiSlide *)instance;
    switch (port) {
        case PORT_INPUT:
            plugin->input = data;
            break;
        case PORT_OUTPUT:
            plugin->output = data;
            break;
        case PORT_BEAT_DIVISOR:
            plugin->beat_divisor = data;
            break;
        case PORT_BEND_SEMITONE_DISTANCE:
            plugin->bend_semitone_distance = data;
            break;
        case PORT_FORCED_VELOCITY:
            plugin->forced_velocity = data;
            break;
    }
}

static void activate(LV2_Handle instance) {
    MidiSlide *plugin = (MidiSlide *)instance;
    plugin->note_stack_size = 0;
    plugin->samples_per_beat = plugin->sample_rate / 2;
    plugin->samples_passed = 0;
    plugin->samples_since_sent = 0;
    plugin->message_interval = plugin->sample_rate / 500;
    plugin->is_sliding = false;
}

static inline bool isAtomObject(uint32_t type, MidiSlideURIs *uris) {
    return (
        type == uris->atom_Object ||
        type == uris->atom_Blank ||
        type == uris->atom_Resource
    );
}

static inline const uint8_t *getMidiMessage(
        const LV2_Atom_Event *event, MidiSlideURIs *uris) {
    if (event->body.type != uris->midi_Event) return NULL;
    return (const uint8_t *)(event + 1);
}

static inline const LV2_Atom_Object *getAtomObject(
        const LV2_Atom_Event *event, MidiSlideURIs *uris) {
    if (!isAtomObject(event->body.type, uris)) return NULL;
    return (const LV2_Atom_Object *)&event->body;
}

static void run(LV2_Handle instance, uint32_t n_samples) {
    MidiSlide *plugin = (MidiSlide *)instance;
    const uint32_t output_capacity = plugin->output[0].atom.size;

    lv2_atom_sequence_clear(plugin->output);
    plugin->output->atom.type = plugin->input->atom.type;

    const LV2_Atom_Sequence *input = plugin->input;
    const LV2_Atom_Event *event = lv2_atom_sequence_begin(&input->body);
    uint32_t last_frames = 0;

    while (!lv2_atom_sequence_is_end(&input->body, input->atom.size, event)) {
        uint32_t frames = event->time.frames;
        uint32_t frame_diff = frames - last_frames;
        last_frames = frames;
        event = run_body(plugin, frame_diff, event, frames, output_capacity);
    }

    if (last_frames < n_samples) {
        run_body(
            plugin, n_samples - last_frames, event, last_frames,
            output_capacity
        );
    }
}

static const LV2_Atom_Event *run_body(
        MidiSlide *plugin, uint32_t n_samples,
        const LV2_Atom_Event *start_event, uint32_t frames,
        uint32_t output_capacity) {
    const LV2_Atom_Sequence *input = plugin->input;
    MidiNote *note_stack = plugin->note_stack;
    uint8_t old_stack_size = plugin->note_stack_size;
    uint8_t old_slide_base, old_slide_top = 0;
    if (old_stack_size >= 2) {
        old_slide_base = note_stack[old_stack_size - 2].key;
        old_slide_top = note_stack[old_stack_size - 1].key;
    }

    const LV2_Atom_Event *event;
    // Loop through events and handle atom objects and all MIDI messages except
    // "note on" messages.
    for (event = start_event;
         !lv2_atom_sequence_is_end(&input->body, input->atom.size, event) && (
             event->time.frames <= frames
         ); event = lv2_atom_sequence_next(event)) {

        const LV2_Atom_Object *object = getAtomObject(event, &plugin->uris);
        if (object != NULL) {
            handle_atom_object(plugin, object);
            continue;
        }

        const uint8_t *midi_message = getMidiMessage(event, &plugin->uris);
        if (midi_message != NULL) {
            MidiAction action = get_midi_action(midi_message);
            if (action == ACTION_NOTE_ON) continue;
            bool handled = handle_midi_message(
                plugin, midi_message, action, output_capacity
            );
            if (!handled) {
                // Forward unchanged MIDI event.
                lv2_atom_sequence_append_event(
                    plugin->output, output_capacity, event
                );
            }
            continue;
        }
    }

    compact_stack(plugin);
    // Whether or not a "note off" message should be sent.
    bool note_stopped = old_stack_size > 0 && plugin->note_stack_size == 0;
    bool force_bend_update = note_stopped || (old_stack_size >= 2 && (
        plugin->note_stack_size < 2 ||
        old_slide_base != note_stack[plugin->note_stack_size - 2].key ||
        old_slide_top != note_stack[plugin->note_stack_size - 1].key
    ));

    if (force_bend_update) plugin->is_sliding = false;
    old_stack_size = plugin->note_stack_size;

    // Loop through events and handle MIDI "note on" messages.
    for (event = start_event;
         !lv2_atom_sequence_is_end(&input->body, input->atom.size, event) && (
             event->time.frames <= frames
         ); event = lv2_atom_sequence_next(event)) {

        const uint8_t *midi_message = getMidiMessage(event, &plugin->uris);
        if (midi_message == NULL) continue;
        if (get_midi_action(midi_message) != ACTION_NOTE_ON) continue;
        handle_midi_message(
            plugin, midi_message, ACTION_NOTE_ON, output_capacity
        );
    }

    bool note_started = old_stack_size == 0 && plugin->note_stack_size > 0;
    // If multiple notes were added at the same time, pick the one with the
    // lowest velocity and move it to the top (end) of the stack.
    move_primary_to_stack_top(plugin, old_stack_size);
    if (plugin->note_stack_size > old_stack_size) {
        // At least one note was added.
        plugin->samples_passed = 0;
        force_bend_update = true;
        if (plugin->note_stack_size >= 2) {
            plugin->is_sliding = true;
        }
    }

    plugin->samples_since_sent += n_samples;
    while (force_bend_update ||
           plugin->samples_since_sent >= plugin->message_interval) {

        bool old_force_update = force_bend_update;
        force_bend_update = false;
        if (!old_force_update) {
            plugin->samples_since_sent -= plugin->message_interval;
        }

        if (old_force_update && note_stopped) {
            stop_note(plugin, frames, output_capacity);
        }
        if (plugin->note_stack_size == 0) continue;

        if (old_force_update && note_started) {
            MidiNote *note;
            if (plugin->note_stack_size >= 2) {
                note = &note_stack[plugin->note_stack_size - 2];
            } else {
                note = &note_stack[0];
            }

            uint8_t key = note->key;
            uint8_t velocity = note->velocity;
            set_bend(plugin, 0, frames, output_capacity);
            play_note(plugin, key, velocity, frames, output_capacity);
            continue;
        }

        if (old_force_update && !plugin->is_sliding) {
            uint8_t key = note_stack[plugin->note_stack_size - 1].key;
            set_bend_from_key(plugin, key, frames, output_capacity);
            continue;
        }
        if (!plugin->is_sliding) continue;

        MidiNote *note = &note_stack[plugin->note_stack_size - 1];
        bool continue_slide = set_bend_from_slide(
            plugin, note->key, note->velocity, (note - 1)->key, frames,
            output_capacity
        );
        if (!continue_slide) plugin->is_sliding = false;

        if (old_force_update) {
        } else {
            if (plugin->is_sliding) {
                plugin->samples_passed += plugin->message_interval;
            }
        }
    }

    return event;
}

static inline void move_primary_to_stack_top(
        MidiSlide *plugin, uint8_t start_at) {
    uint8_t note_stack_size = plugin->note_stack_size;
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wstrict-overflow"
        if (note_stack_size <= 1) return;
        if (start_at > note_stack_size - 2) return;
    #pragma GCC diagnostic pop
    MidiNote *note_stack = plugin->note_stack;

    uint8_t min_vel_index = note_stack_size - 1;
    uint8_t min_vel = note_stack[min_vel_index].velocity;
    for (int i = min_vel_index; i >= start_at; i--) {
        uint8_t velocity = note_stack[i].velocity;
        if (velocity < min_vel) {
            min_vel = velocity;
            min_vel_index = (uint8_t)i;
        }
    }

    if (min_vel_index < note_stack_size - 1) {
        MidiNote temp = note_stack[note_stack_size - 1];
        note_stack[note_stack_size - 1] = note_stack[min_vel_index];
        note_stack[min_vel_index] = temp;

        uint8_t *key_to_pos = plugin->key_to_stack_pos;
        key_to_pos[note_stack[note_stack_size - 1].key] = note_stack_size - 1;
        key_to_pos[note_stack[min_vel_index].key] = min_vel_index;
    }
}

static inline bool set_bend_from_slide(
        MidiSlide *plugin, uint8_t key, uint8_t velocity, uint8_t base_key,
        uint32_t frames, uint32_t output_capacity) {
    float beat_divisor = *plugin->beat_divisor;
    uint32_t samples_per_beat = plugin->samples_per_beat;
    uint32_t slide_duration = (samples_per_beat * velocity) / beat_divisor;
    uint32_t samples_passed = plugin->samples_passed;
    if (samples_passed > slide_duration * 2) return false;
    if (samples_passed > slide_duration) {
        samples_passed = 2 * slide_duration - samples_passed;
    }

    int key_diff = (int)key - base_key;
    int key_offset = (int)base_key - plugin->key_playing;
    if (abs(key_offset) > *plugin->bend_semitone_distance) return false;
    if (abs(key_diff + key_offset) > *plugin->bend_semitone_distance) {
        return false;
    }

    double relative_key = (
        ((double)samples_passed / slide_duration) * key_diff + key_offset
    );
    int bend_value = relative_key_to_bend(plugin, relative_key);
    set_bend(plugin, bend_value, frames, output_capacity);
    return true;
}

static inline void set_bend_from_key(
        MidiSlide *plugin, uint8_t key, uint32_t frames,
        uint32_t output_capacity) {
    int relative_key = (int)key - plugin->key_playing;
    if (abs(relative_key) > *plugin->bend_semitone_distance) return;
    int bend_value = relative_key_to_bend(plugin, relative_key);
    set_bend(plugin, bend_value, frames, output_capacity);
}

static inline int relative_key_to_bend(
        MidiSlide *plugin, double relative_key) {
    int bend_multiplier = relative_key < 0 ? 8192 : 8191;
    return bend_multiplier * relative_key / *plugin->bend_semitone_distance;
}

static inline void init_midi_event(
        MidiSlide *plugin, MidiEvent *event, uint32_t frames) {
    event->event.time.frames = frames;
    event->event.body.type = plugin->uris.midi_Event;
    event->event.body.size = 3;
}

static inline void set_bend(
        MidiSlide *plugin, int value, uint32_t frames,
        uint32_t output_capacity) {
    uint16_t real_bend = value + 8192;
    MidiEvent event;
    init_midi_event(plugin, &event, frames);
    event.message[0] = LV2_MIDI_MSG_BENDER;
    event.message[1] = real_bend & 0x7f;
    event.message[2] = (real_bend >> 7) & 0x7f;
    send_midi_message(plugin, &event, output_capacity);
}

static inline void play_note(
        MidiSlide *plugin, uint8_t key, uint8_t velocity, uint32_t frames,
        uint32_t output_capacity) {
    MidiEvent event;
    init_midi_event(plugin, &event, frames);
    plugin->key_playing = key;
    if (*plugin->forced_velocity > 0) velocity = *plugin->forced_velocity;
    event.message[0] = LV2_MIDI_MSG_NOTE_ON;
    event.message[1] = key;
    event.message[2] = velocity;
    send_midi_message(plugin, &event, output_capacity);
}

static inline void stop_note(
        MidiSlide *plugin, uint32_t frames, uint32_t output_capacity) {
    MidiEvent event;
    init_midi_event(plugin, &event, frames);
    event.message[0] = LV2_MIDI_MSG_NOTE_OFF;
    event.message[1] = plugin->key_playing;
    event.message[2] = 0;  // For now, zero velocity.
    send_midi_message(plugin, &event, output_capacity);
}

static inline void send_midi_message(
        MidiSlide *plugin, MidiEvent *event, uint32_t output_capacity) {
    LV2_Atom_Event *result = lv2_atom_sequence_append_event(
        plugin->output, output_capacity, &event->event
    );
    if (result == NULL) {
        fprintf(stderr, "Error: Could not append atom event.\n");
    }
}

static inline bool handle_midi_message(
        MidiSlide *plugin, const uint8_t *message, MidiAction action,
        uint32_t output_capacity) {
    switch (action) {
        case ACTION_NOTE_ON:
            handle_note_on(plugin, message[1], message[2]);
            break;
        case ACTION_NOTE_OFF:
            handle_note_off(plugin, message[1]);
            break;
        case ACTION_ALL_NOTES_OFF:
            handle_all_notes_off(plugin);
            break;
        default:
            return false;
    }
    return true;
}

static inline MidiAction get_midi_action(const uint8_t *message) {
    uint8_t message_type = message[0] & 0xF0;
    switch (message_type) {
        case LV2_MIDI_MSG_NOTE_ON:
            return ACTION_NOTE_ON;
        case LV2_MIDI_MSG_NOTE_OFF:
            return ACTION_NOTE_OFF;
        case LV2_MIDI_MSG_CONTROLLER: ;
            uint8_t controller = message[1];
            if (controller >= LV2_MIDI_CTL_ALL_NOTES_OFF &&
                controller <= LV2_MIDI_CTL_MONO2) {
                // All messages in this range turn off notes.
                return ACTION_ALL_NOTES_OFF;
            }
            break;
    }
    return ACTION_UNKNOWN;
}

static inline void handle_note_on(
        MidiSlide *plugin, uint8_t key, uint8_t velocity) {
    add_to_stack(plugin, key, velocity);
}

static inline void handle_note_off(MidiSlide *plugin, uint8_t key) {
    remove_from_stack(plugin, key);
}

static inline void handle_all_notes_off(MidiSlide *plugin) {
    clear_stack(plugin);
}

static inline void add_to_stack(
        MidiSlide *plugin, uint8_t key, uint8_t velocity) {

    uint8_t stack_size = plugin->note_stack_size;
    if (stack_size >= 128) {
        fprintf(stderr, "Error: Note stack is full.\n");
        return;
    }

    uint8_t old_stack_pos = plugin->key_to_stack_pos[key];
    if (old_stack_pos < plugin->note_stack_size &&
        plugin->note_stack[old_stack_pos].key == key) {
        // Note is already in the stack.
        fprintf(stderr, "Error: Note is already in stack.\n");
        return;
    }

    plugin->note_stack[stack_size] = (MidiNote){
        .active = true,
        .key = key,
        .velocity = velocity,
    };
    plugin->key_to_stack_pos[key] = stack_size;
    plugin->note_stack_size++;
}

static inline void remove_from_stack(MidiSlide *plugin, uint8_t key) {
    uint8_t stack_pos = plugin->key_to_stack_pos[key];
    uint8_t stack_size = plugin->note_stack_size;

    if (stack_pos >= stack_size) {
        fprintf(stderr, "Error: Note is not in stack.\n");
        return;
    }

    if (plugin->note_stack[stack_pos].key != key) {
        fprintf(stderr, "Error: Note is not in stack.\n");
        return;
    }

    plugin->note_stack[stack_pos].active = false;
}

static inline void clear_stack(MidiSlide *plugin) {
    plugin->note_stack_size = 0;
}

static inline void compact_stack(MidiSlide *plugin) {
    MidiNote *stack = plugin->note_stack;
    uint8_t *key_to_stack_pos = plugin->key_to_stack_pos;
    uint8_t stack_size = plugin->note_stack_size;
    uint8_t offset = 0;
    for (uint8_t i = 0; i + offset < stack_size; i++) {
        while (i + offset < stack_size && !stack[i + offset].active) {
            offset++;
        }
        if (offset > 0 && i + offset < stack_size) {
            stack[i] = stack[i + offset];
            key_to_stack_pos[stack[i].key] = i;
        }
    }
    plugin->note_stack_size -= offset;
}

static inline void handle_atom_object(
        MidiSlide *plugin, const LV2_Atom_Object *object) {
    if (object->body.otype != plugin->uris.time_Position) return;
    LV2_Atom *bpm = NULL;
    lv2_atom_object_get(object, plugin->uris.time_beatsPerMinute, &bpm, NULL);
    if (bpm == NULL) return;
    float bpm_float = ((LV2_Atom_Float *)bpm)->body;
    plugin->samples_per_beat = 60.0 / bpm_float * plugin->sample_rate;
}

static void deactivate(LV2_Handle instance) {
}

static void cleanup(LV2_Handle instance) {
    MidiSlide *plugin = (MidiSlide *)instance;
    free(plugin);
}

static const void *extension_data(const char *uri) {
    return NULL;
}

static const LV2_Descriptor descriptor = {
    PLUGIN_URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor *lv2_descriptor(uint32_t index) {
    switch (index) {
        case 0:
            return &descriptor;
        default:
            return NULL;
    }
}
