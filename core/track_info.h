/*
 * What's playing and where the station lives on the web:
 *
 *   - splitting a stream title ("Artist - Title") into artist and title,
 *   - artist and title from an ID3v2 tag (HLS streams carry them so),
 *   - a station's website from its notes ("web:https://...").
 */
#ifndef DXG_TRACK_INFO_H
#define DXG_TRACK_INFO_H

#include <stddef.h>

/* Splits at the first " - " (or an en or em dash with spaces around it)
 * when there's text on both sides. Returns 1 if split; otherwise the whole
 * title goes to `title` and artist is "". */
int title_split(const char *in, char *artist, size_t an, char *title, size_t tn);

/* TPE1 (artist) and TIT2 (title) from the ID3v2.2/2.3/2.4 tag at p, as
 * UTF-8. Returns 1 if either was found. */
int id3_track(const unsigned char *p, size_t n, char *artist, size_t an, char *title, size_t tn);

/* The http(s) address after "web:" in a station's notes. Returns 1 if
 * found. */
int notes_web(const char *notes, char *url, size_t n);

/* Notes without the "web:" and "rb:" tokens, for showing to people
 * ("MP3 128k"). */
void notes_display(const char *notes, char *out, size_t n);

#endif
