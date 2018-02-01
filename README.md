Fish MidiSlide
==============

Fish MidiSlide is an LV2 plugin that allows you to use slide notes with plugins
that accept MIDI input. Any plugin that supports the MIDI pitch bend controller
is supported.


Usage
-----

“Pitch bend semitone distance” should be set to the distance, in semitones,
from the middle of the pitch bend range (i.e., the note generated without any
pitch bend) to the highest (or lowest) possible pitch bend value (i.e., the
note generated with the pitch bend value at one of the extremes).

The maximum distance between slide notes is limited by this value. Ideally, the
target synthesizer allows you to set the pitch bend range to a large value.

Due to limitations of MIDI, the lengths of notes can’t be known to plugins
until after the notes have stopped playing. Because this plugin needs to know
the lengths of slide notes as soon as they start, these lengths must be
specified in an additional way.

Currently, this is done by setting the velocity of slide notes. In a slide from
note A to note B, the velocity of note B determines the length of the slide.
When the “Beat divisor” control is 1, the velocity indicates the length of the
slide from A to B in beats. When “Beat divisor” is 2, half-beats are used, and
so on.

For example if “Beat divisor” is set to 4, and a slide from note A to B that
lasts 1.5 beats is desired, A and B should overlap by exactly 1.5 beats, and
B’s velocity should be set to 6 (since 1.5 × 4 = 6).

Slides from A to B immediately followed by a slide back from B to A are also
supported. To do this, set B’s length to twice the length indicated by the
velocity, and ensure that A and B overlap for the entire duration of B.

For example, if “Beat divisor” is 4 and a slide from A to B for 2.5 beats,
followed by a slide from B to A for 2.5 beats, is desired, set B’s velocity to
10, set B’s length to 5 beats, and ensure that A and B overlap for the entire
duration of B.

The “Fixed velocity” setting overrides the velocity of every audible note with
the specified value.


Dependencies
------------

* LV2 development files
* GCC
* GNU Make

On Debian GNU/Linux (and many derivatives), these can be installed by running
the following command as root:

```
apt-get install lv2-dev gcc make
```


Installation
------------

Run the following commands (you will need to have [Git] installed):

```
git clone https://github.com/taylordotfish/midislide ~/.lv2/fish-midislide.lv2/
cd ~/.lv2/fish-midislide.lv2/
make
```

[Git]: https://git-scm.com/


License
-------

Fish MidiSlide is licensed under the GNU General Public License, version 3 or
any later version. See [LICENSE].

This README file has been released to the public domain using [CC0].

[LICENSE]: LICENSE
[CC0]: https://creativecommons.org/publicdomain/zero/1.0/
