# Copyright (C) 2018 taylor.fish <contact@taylor.fish>
#
# This file is part of Fish MidiSlide.
#
# Fish MidiSlide is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Fish MidiSlide is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Fish MidiSlide.  If not, see <http://www.gnu.org/licenses/>.

CC = gcc
CFLAGS = -Wall -Wextra -Werror -pedantic -std=c99 -fpic -Ofast -MMD
LDFLAGS = -shared -Wl,--no-undefined,--no-allow-shlib-undefined
LDLIBS = -lm
OBJECTS = midislide.o
LIBRARY = midislide.so

all: $(LIBRARY)

$(LIBRARY): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

-include $(OBJECTS:.o=.d)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: clean
clean:
	rm -f *.o *.d *.so
