/*
 * Copyright (C) 2018 taylor.fish <contact@taylor.fish>
 *
 * This file is part of Fish MidiSlide.
 *
 * Fish MidiSlide is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Fish MidiSlide is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Fish MidiSlide.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MIDISLIDE_H
#define MIDISLIDE_H

#include "ports.h"
#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/atom/util.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/time/time.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>

typedef struct {
    LV2_URID midi_Event;
    LV2_URID atom_Float;
    LV2_URID time_Position;
    LV2_URID time_beatsPerMinute;
    LV2_URID atom_Object;
    LV2_URID atom_Blank;
    LV2_URID atom_Resource;
} MidiSlideURIs;

typedef struct {
    bool active;
    uint8_t key;
    uint8_t velocity;
} MidiNote;

typedef enum {
    ACTION_UNKNOWN,
    ACTION_NOTE_ON,
    ACTION_NOTE_OFF,
    ACTION_ALL_NOTES_OFF,
} MidiAction;

typedef struct {
    LV2_Atom_Event event;
    uint8_t message[3];
} MidiEvent;

typedef struct {
    const float *beat_divisor;
    const float *bend_semitone_distance;
    const float *forced_velocity;

    LV2_URID_Map *map;
    const LV2_Atom_Sequence *input;
    LV2_Atom_Sequence *output;
    MidiSlideURIs uris;

    uint32_t sample_rate;
    uint32_t samples_per_beat;
    uint32_t samples_passed;
    uint32_t samples_since_sent;
    uint32_t message_interval;
    uint8_t key_playing;
    bool is_sliding;

    MidiNote note_stack[128];
    uint8_t note_stack_size;
    uint8_t key_to_stack_pos[128];
} MidiSlide;

LV2_SYMBOL_EXPORT
const LV2_Descriptor *lv2_descriptor(uint32_t index);

#endif
