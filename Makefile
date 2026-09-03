CXX ?= g++
CXXFLAGS ?= -O2 -pipe
CXXFLAGS += -std=c++20 -Wall -Wextra -Wpedantic
PKGS = gtk+-3.0 json-glib-1.0 pangocairo epoxy vte-2.91
BINUP_PKGS = libcurl
PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin

.PHONY: all clean install

all: elephant-field binup

elephant-field: main.cpp
	$(CXX) $(CXXFLAGS) $(shell pkg-config --cflags $(PKGS)) $< -o $@ $(shell pkg-config --libs $(PKGS))

binup: tools/binup.cpp
	$(CXX) $(CXXFLAGS) $(shell pkg-config --cflags $(BINUP_PKGS)) $< -o $@ $(shell pkg-config --libs $(BINUP_PKGS))

install: elephant-field binup
	install -Dm755 elephant-field $(DESTDIR)$(BINDIR)/elephant-field
	ln -sfn elephant-field $(DESTDIR)$(BINDIR)/kalwer
	install -Dm755 binup $(DESTDIR)$(BINDIR)/binup
	install -Dm644 assets/kalwer.svg $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/kalwer.svg
	install -Dm644 kalwer.desktop $(DESTDIR)$(PREFIX)/share/applications/kalwer.desktop

clean:
	rm -f elephant-field binup
