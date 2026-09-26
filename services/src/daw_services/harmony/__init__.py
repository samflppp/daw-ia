"""Harmonic engine, corpus side: the MIDI files of the corpus counted into the style model.

The generator itself runs in C++ (core/domain/generation): it has to answer in
under 16 ms, with or without this process. What lives here is offline: reading
MIDI (midi), the rules shared with the generator (theory), and the pipeline
that writes the style model's tables (corpus).
"""
