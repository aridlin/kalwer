CXX ?= g++
CXXFLAGS ?= -O2 -pipe
CXXFLAGS += -std=c++20 -Wall -Wextra -Wpedantic
PKGS = gtk+-3.0 json-glib-1.0 pangocairo epoxy vte-2.91
PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin

.PHONY: all clean install

all: elephant-field

elephant-field: main.cpp
	$(CXX) $(CXXFLAGS) $(shell pkg-config --cflags $(PKGS)) $< -o $@ $(shell pkg-config --libs $(PKGS))

install: elephant-field
	install -Dm755 elephant-field $(DESTDIR)$(BINDIR)/elephant-field
	ln -sfn elephant-field $(DESTDIR)$(BINDIR)/kalwer

clean:
	rm -f elephant-field
