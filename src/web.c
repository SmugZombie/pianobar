/*
Copyright (c) 2024
	Lars-Dominik Braun <lars@6xq.net>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifdef HAVE_MICROHTTPD
#include <microhttpd.h>
#endif

#include "web.h"
#include "main.h"
#include "ui_dispatch.h"
#include "ui.h"
#include "player.h"
#include "debug.h"

static BarApp_t *g_app = NULL;
static pthread_mutex_t g_app_mutex = PTHREAD_MUTEX_INITIALIZER;
#ifdef HAVE_MICROHTTPD
static struct MHD_Daemon *g_daemon = NULL;
#endif

/* HTML/JS frontend */
static const char *html_frontend =
"<!DOCTYPE html>\n"
"<html>\n"
"<head>\n"
"<meta charset=\"UTF-8\">\n"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
"<title>Pianobar Remote Control</title>\n"
"<style>\n"
"* { margin: 0; padding: 0; box-sizing: border-box; }\n"
"body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: #1a1a1a; color: #fff; padding: 20px; }\n"
".container { max-width: 800px; margin: 0 auto; }\n"
".header { text-align: center; margin-bottom: 30px; }\n"
".header h1 { font-size: 2em; margin-bottom: 10px; }\n"
".now-playing { background: #2a2a2a; border-radius: 10px; padding: 20px; margin-bottom: 20px; }\n"
".song-info { margin-bottom: 15px; }\n"
".song-title { font-size: 1.5em; font-weight: bold; margin-bottom: 5px; }\n"
".song-artist { font-size: 1.1em; color: #aaa; margin-bottom: 5px; }\n"
".song-album { font-size: 0.9em; color: #888; }\n"
".song-station { margin-top: 10px; font-size: 0.9em; color: #666; }\n"
".controls { display: flex; gap: 10px; flex-wrap: wrap; margin-top: 20px; }\n"
".btn { padding: 12px 24px; border: none; border-radius: 5px; cursor: pointer; font-size: 1em; transition: background 0.2s; }\n"
".btn-primary { background: #4a9eff; color: white; }\n"
".btn-primary:hover { background: #3a8eef; }\n"
".btn-danger { background: #ff4a4a; color: white; }\n"
".btn-danger:hover { background: #ef3a3a; }\n"
".btn-success { background: #4aff4a; color: #1a1a1a; }\n"
".btn-success:hover { background: #3aef3a; }\n"
".btn-secondary { background: #4a4a4a; color: white; }\n"
".btn-secondary:hover { background: #5a5a5a; }\n"
".btn:disabled { opacity: 0.5; cursor: not-allowed; }\n"
".status { margin-top: 15px; padding: 10px; border-radius: 5px; background: #333; }\n"
".status.playing { background: #2a4a2a; }\n"
".status.paused { background: #4a4a2a; }\n"
".status.stopped { background: #4a2a2a; }\n"
".time-info { margin-top: 10px; font-size: 0.9em; color: #aaa; }\n"
".stations { background: #2a2a2a; border-radius: 10px; padding: 20px; margin-top: 20px; }\n"
".stations h2 { margin-bottom: 15px; }\n"
".station-list { display: flex; flex-direction: column; gap: 10px; }\n"
".station-item { padding: 10px; background: #333; border-radius: 5px; cursor: pointer; transition: background 0.2s; }\n"
".station-item:hover { background: #3a3a3a; }\n"
".station-item.active { background: #4a9eff; }\n"
"</style>\n"
"</head>\n"
"<body>\n"
"<div class=\"container\">\n"
"<div class=\"header\">\n"
"<h1>🎹 Pianobar Remote Control</h1>\n"
"</div>\n"
"<div class=\"now-playing\">\n"
"<div class=\"song-info\">\n"
"<div class=\"song-title\" id=\"song-title\">No song playing</div>\n"
"<div class=\"song-artist\" id=\"song-artist\"></div>\n"
"<div class=\"song-album\" id=\"song-album\"></div>\n"
"<div class=\"song-station\" id=\"song-station\"></div>\n"
"</div>\n"
"<div class=\"time-info\" id=\"time-info\"></div>\n"
"<div class=\"status\" id=\"status\">Stopped</div>\n"
"<div class=\"controls\">\n"
"<button class=\"btn btn-primary\" onclick=\"skipSong()\">Skip</button>\n"
"<button class=\"btn btn-success\" onclick=\"loveSong()\">❤️ Love</button>\n"
"<button class=\"btn btn-danger\" onclick=\"banSong()\">🚫 Ban</button>\n"
"<button class=\"btn btn-secondary\" onclick=\"togglePause()\" id=\"pause-btn\">Pause</button>\n"
"</div>\n"
"</div>\n"
"<div class=\"stations\">\n"
"<h2>Stations</h2>\n"
"<div class=\"station-list\" id=\"station-list\">Loading...</div>\n"
"</div>\n"
"</div>\n"
"<script>\n"
"let updateInterval;\n"
"function updateStatus() {\n"
"fetch('/api/status')\n"
".then(r => r.json())\n"
".then(data => {\n"
"document.getElementById('song-title').textContent = data.song ? data.song.title : 'No song playing';\n"
"document.getElementById('song-artist').textContent = data.song ? data.song.artist : '';\n"
"document.getElementById('song-album').textContent = data.song ? data.song.album || '' : '';\n"
"document.getElementById('song-station').textContent = data.station ? 'Station: ' + data.station.name : '';\n"
"const statusEl = document.getElementById('status');\n"
"statusEl.className = 'status ' + data.player_state;\n"
"statusEl.textContent = data.player_state.charAt(0).toUpperCase() + data.player_state.slice(1);\n"
"const pauseBtn = document.getElementById('pause-btn');\n"
"if (data.player_state === 'playing') {\n"
"pauseBtn.textContent = 'Pause';\n"
"} else if (data.player_state === 'paused') {\n"
"pauseBtn.textContent = 'Resume';\n"
"} else {\n"
"pauseBtn.textContent = 'Play';\n"
"}\n"
"if (data.song && data.time_played !== undefined && data.time_total !== undefined) {\n"
"const played = Math.floor(data.time_played);\n"
"const total = Math.floor(data.time_total);\n"
"const playedMin = Math.floor(played / 60);\n"
"const playedSec = played % 60;\n"
"const totalMin = Math.floor(total / 60);\n"
"const totalSec = total % 60;\n"
"document.getElementById('time-info').textContent = \n"
"String(playedMin).padStart(2, '0') + ':' + String(playedSec).padStart(2, '0') + ' / ' +\n"
"String(totalMin).padStart(2, '0') + ':' + String(totalSec).padStart(2, '0');\n"
"} else {\n"
"document.getElementById('time-info').textContent = '';\n"
"}\n"
"})\n"
".catch(err => console.error('Error:', err));\n"
"}\n"
"function updateStations() {\n"
"fetch('/api/stations')\n"
".then(r => r.json())\n"
".then(data => {\n"
"const list = document.getElementById('station-list');\n"
"if (data.stations && data.stations.length > 0) {\n"
"list.innerHTML = data.stations.map(s => \n"
"'<div class=\"station-item' + (s.active ? ' active' : '') + '\" onclick=\"changeStation(\\'' + s.id + '\\')\">' +\n"
"s.name + '</div>'\n"
").join('');\n"
"} else {\n"
"list.innerHTML = '<div>No stations available</div>';\n"
"}\n"
"})\n"
".catch(err => console.error('Error:', err));\n"
"}\n"
"function skipSong() { fetch('/api/skip', {method: 'POST'}).then(() => setTimeout(updateStatus, 500)); }\n"
"function loveSong() { fetch('/api/love', {method: 'POST'}).then(() => setTimeout(updateStatus, 500)); }\n"
"function banSong() { fetch('/api/ban', {method: 'POST'}).then(() => setTimeout(updateStatus, 500)); }\n"
"function togglePause() { fetch('/api/pause', {method: 'POST'}).then(() => setTimeout(updateStatus, 500)); }\n"
"function changeStation(id) { \n"
"console.log('Changing station to:', id);\n"
"fetch('/api/station', {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({id: id})})\n"
".then(r => r.json())\n"
".then(data => {\n"
"console.log('Station change response:', data);\n"
"if (data.success) {\n"
"setTimeout(updateStatus, 500);\n"
"setTimeout(updateStations, 500);\n"
"} else {\n"
"alert('Failed to change station: ' + (data.error || 'Unknown error'));\n"
"}\n"
"})\n"
".catch(err => {\n"
"console.error('Station change error:', err);\n"
"alert('Error changing station: ' + err.message);\n"
"});\n"
"}\n"
"updateStatus();\n"
"updateStations();\n"
"updateInterval = setInterval(() => { updateStatus(); }, 1000);\n"
"setInterval(updateStations, 5000);\n"
"</script>\n"
"</body>\n"
"</html>\n";

#ifdef HAVE_MICROHTTPD
static int send_json_response(struct MHD_Connection *connection, const char *json, int status_code) {
	struct MHD_Response *response = MHD_create_response_from_buffer(
		strlen(json), (void *)json, MHD_RESPMEM_PERSISTENT);
	if (!response) return MHD_NO;
	
	MHD_add_response_header(response, "Content-Type", "application/json");
	MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
	int ret = MHD_queue_response(connection, status_code, response);
	MHD_destroy_response(response);
	return ret;
}

struct post_data {
	char *data;
	size_t size;
};

static void free_post_data(void *cls) {
	struct post_data *pd = (struct post_data *)cls;
	if (pd) {
		free(pd->data);
		free(pd);
	}
}

static int send_html_response(struct MHD_Connection *connection, const char *html) {
	struct MHD_Response *response = MHD_create_response_from_buffer(
		strlen(html), (void *)html, MHD_RESPMEM_PERSISTENT);
	if (!response) return MHD_NO;
	
	MHD_add_response_header(response, "Content-Type", "text/html");
	int ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
	MHD_destroy_response(response);
	return ret;
}

/* Simple JSON string escaping */
static void json_escape_string(char *dest, size_t destsize, const char *src) {
	if (!src) {
		dest[0] = '\0';
		return;
	}
	
	size_t pos = 0;
	for (const char *p = src; *p && pos < destsize - 1; p++) {
		switch (*p) {
			case '"': 
				if (pos + 2 < destsize) { dest[pos++] = '\\'; dest[pos++] = '"'; }
				break;
			case '\\':
				if (pos + 2 < destsize) { dest[pos++] = '\\'; dest[pos++] = '\\'; }
				break;
			case '\n':
				if (pos + 2 < destsize) { dest[pos++] = '\\'; dest[pos++] = 'n'; }
				break;
			case '\r':
				if (pos + 2 < destsize) { dest[pos++] = '\\'; dest[pos++] = 'r'; }
				break;
			case '\t':
				if (pos + 2 < destsize) { dest[pos++] = '\\'; dest[pos++] = 't'; }
				break;
			default:
				dest[pos++] = *p;
				break;
		}
	}
	dest[pos] = '\0';
}

static int get_status_json(char *buf, size_t bufsize) {
	if (!g_app) {
		snprintf(buf, bufsize, "{\"error\":\"Application not initialized\"}");
		return -1;
	}
	
	pthread_mutex_lock(&g_app_mutex);
	
	const PianoSong_t *song = g_app->playlist;
	const PianoStation_t *station = g_app->curStation;
	BarPlayerMode mode = BarPlayerGetMode(&g_app->player);
	
	unsigned int songPlayed = 0, songDuration = 0;
	pthread_mutex_lock(&g_app->player.lock);
	songPlayed = g_app->player.songPlayed;
	songDuration = g_app->player.songDuration;
	pthread_mutex_unlock(&g_app->player.lock);
	
	const char *state_str = "stopped";
	if (mode == PLAYER_PLAYING) {
		state_str = "playing";
	} else if (mode == PLAYER_WAITING) {
		state_str = "waiting";
	} else if (mode == PLAYER_FINISHED) {
		state_str = "finished";
	}
	
	char title_esc[512], artist_esc[512], album_esc[512], cover_esc[512];
	char station_name_esc[256], station_id_esc[128];
	
	if (song) {
		json_escape_string(title_esc, sizeof(title_esc), song->title);
		json_escape_string(artist_esc, sizeof(artist_esc), song->artist);
		json_escape_string(album_esc, sizeof(album_esc), song->album);
		json_escape_string(cover_esc, sizeof(cover_esc), song->coverArt);
	}
	
	if (station) {
		json_escape_string(station_name_esc, sizeof(station_name_esc), station->name);
		json_escape_string(station_id_esc, sizeof(station_id_esc), station->id);
	}
	
	if (song) {
		if (station) {
			snprintf(buf, bufsize,
				"{"
				"\"player_state\":\"%s\","
				"\"song\":{"
				"\"title\":\"%s\","
				"\"artist\":\"%s\","
				"\"album\":\"%s\","
				"\"coverArt\":\"%s\","
				"\"rating\":%d"
				"},"
				"\"station\":{"
				"\"name\":\"%s\","
				"\"id\":\"%s\""
				"},"
				"\"time_played\":%u,"
				"\"time_total\":%u"
				"}",
				state_str,
				title_esc, artist_esc, album_esc, cover_esc,
				song->rating,
				station_name_esc, station_id_esc,
				songPlayed, songDuration
			);
		} else {
			snprintf(buf, bufsize,
				"{"
				"\"player_state\":\"%s\","
				"\"song\":{"
				"\"title\":\"%s\","
				"\"artist\":\"%s\","
				"\"album\":\"%s\","
				"\"coverArt\":\"%s\","
				"\"rating\":%d"
				"},"
				"\"station\":null,"
				"\"time_played\":%u,"
				"\"time_total\":%u"
				"}",
				state_str,
				title_esc, artist_esc, album_esc, cover_esc,
				song->rating,
				songPlayed, songDuration
			);
		}
	} else {
		if (station) {
			snprintf(buf, bufsize,
				"{"
				"\"player_state\":\"%s\","
				"\"song\":null,"
				"\"station\":{"
				"\"name\":\"%s\","
				"\"id\":\"%s\""
				"},"
				"\"time_played\":0,"
				"\"time_total\":0"
				"}",
				state_str,
				station_name_esc, station_id_esc
			);
		} else {
			snprintf(buf, bufsize,
				"{"
				"\"player_state\":\"%s\","
				"\"song\":null,"
				"\"station\":null,"
				"\"time_played\":0,"
				"\"time_total\":0"
				"}",
				state_str
			);
		}
	}
	
	pthread_mutex_unlock(&g_app_mutex);
	return 0;
}

static int get_stations_json(char *buf, size_t bufsize) {
	if (!g_app) {
		snprintf(buf, bufsize, "{\"error\":\"Application not initialized\"}");
		return -1;
	}
	
	pthread_mutex_lock(&g_app_mutex);
	
	const PianoStation_t *curStation = g_app->curStation;
	const PianoStation_t *stations = g_app->ph.stations;
	
	char *pos = buf;
	size_t remaining = bufsize;
	int ret;
	
	ret = snprintf(pos, remaining, "{\"stations\":[");
	if (ret < 0 || (size_t)ret >= remaining) {
		pthread_mutex_unlock(&g_app_mutex);
		return -1;
	}
	pos += ret;
	remaining -= ret;
	
	bool first = true;
	char name_esc[256], id_esc[128];
	
	PianoListForeachP (stations) {
		if (!first) {
			ret = snprintf(pos, remaining, ",");
			if (ret < 0 || (size_t)ret >= remaining) {
				pthread_mutex_unlock(&g_app_mutex);
				return -1;
			}
			pos += ret;
			remaining -= ret;
		}
		first = false;
		
		json_escape_string(name_esc, sizeof(name_esc), stations->name);
		json_escape_string(id_esc, sizeof(id_esc), stations->id);
		bool active = (curStation == stations);
		
		ret = snprintf(pos, remaining,
			"{\"id\":\"%s\",\"name\":\"%s\",\"active\":%s}",
			id_esc, name_esc, active ? "true" : "false");
		if (ret < 0 || (size_t)ret >= remaining) {
			pthread_mutex_unlock(&g_app_mutex);
			return -1;
		}
		pos += ret;
		remaining -= ret;
	}
	
	ret = snprintf(pos, remaining, "]}");
	if (ret < 0 || (size_t)ret >= remaining) {
		pthread_mutex_unlock(&g_app_mutex);
		return -1;
	}
	
	pthread_mutex_unlock(&g_app_mutex);
	return 0;
}

static enum MHD_Result handle_request(void *cls, struct MHD_Connection *connection,
		const char *url, const char *method, const char *version,
		const char *upload_data, size_t *upload_data_size, void **con_cls) {
	
	(void)cls;
	(void)version;
	
	struct post_data *pd = *con_cls ? (struct post_data *)*con_cls : NULL;
	
	/* Handle POST data accumulation */
	if (strcmp(method, "POST") == 0) {
		if (!pd) {
			/* First call for this POST request - initialize and wait for data */
			pd = calloc(1, sizeof(struct post_data));
			if (!pd) return MHD_NO;
			pd->data = NULL;
			pd->size = 0;
			*con_cls = pd;
			/* Return MHD_YES to wait for POST data */
			return MHD_YES;
		}
		
		/* Accumulate raw POST data (for JSON) */
		if (*upload_data_size > 0) {
			if (upload_data == NULL) {
				if (pd) free_post_data(pd);
				return MHD_NO;
			}
			size_t new_size = pd->size + *upload_data_size;
			if (new_size < 4096) {
				char *new_data = realloc(pd->data, new_size + 1);
				if (new_data) {
					pd->data = new_data;
					memcpy(pd->data + pd->size, upload_data, *upload_data_size);
					pd->size = new_size;
					pd->data[pd->size] = '\0';
				} else {
					if (pd) free_post_data(pd);
					return MHD_NO;
				}
			} else {
				if (pd) free_post_data(pd);
				return send_json_response(connection, "{\"error\":\"POST data too large\"}", MHD_HTTP_REQUEST_ENTITY_TOO_LARGE);
			}
			*upload_data_size = 0;
			return MHD_YES; /* Continue receiving data */
		}
		/* POST data complete (*upload_data_size == 0) - all chunks received */
		/* Fall through to process request - pd should contain all accumulated POST data */
	}
	
	if (strcmp(method, "GET") == 0) {
		if (strcmp(url, "/") == 0 || strcmp(url, "/index.html") == 0) {
			return send_html_response(connection, html_frontend);
		} else if (strcmp(url, "/api/status") == 0) {
			char json[2048];
			if (get_status_json(json, sizeof(json)) == 0) {
				return send_json_response(connection, json, MHD_HTTP_OK);
			} else {
				return send_json_response(connection, "{\"error\":\"Failed to get status\"}", MHD_HTTP_INTERNAL_SERVER_ERROR);
			}
		} else if (strcmp(url, "/api/stations") == 0) {
			char json[8192];
			if (get_stations_json(json, sizeof(json)) == 0) {
				return send_json_response(connection, json, MHD_HTTP_OK);
			} else {
				return send_json_response(connection, "{\"error\":\"Failed to get stations\"}", MHD_HTTP_INTERNAL_SERVER_ERROR);
			}
		}
	} else if (strcmp(method, "POST") == 0) {
		if (strcmp(url, "/api/skip") == 0) {
			if (g_app && g_app->settings.keys[BAR_KS_SKIP] != BAR_KS_DISABLED) {
				pthread_mutex_lock(&g_app_mutex);
				BarUiDispatch(g_app, g_app->settings.keys[BAR_KS_SKIP],
					g_app->curStation, g_app->playlist, false, BAR_DC_GLOBAL | BAR_DC_STATION);
				pthread_mutex_unlock(&g_app_mutex);
				if (pd) free_post_data(pd);
				return send_json_response(connection, "{\"success\":true}", MHD_HTTP_OK);
			}
		} else if (strcmp(url, "/api/love") == 0) {
			if (g_app && g_app->settings.keys[BAR_KS_LOVE] != BAR_KS_DISABLED) {
				pthread_mutex_lock(&g_app_mutex);
				BarUiDispatch(g_app, g_app->settings.keys[BAR_KS_LOVE],
					g_app->curStation, g_app->playlist, false, BAR_DC_GLOBAL | BAR_DC_STATION | BAR_DC_SONG);
				pthread_mutex_unlock(&g_app_mutex);
				if (pd) free_post_data(pd);
				return send_json_response(connection, "{\"success\":true}", MHD_HTTP_OK);
			}
		} else if (strcmp(url, "/api/ban") == 0) {
			if (g_app && g_app->settings.keys[BAR_KS_BAN] != BAR_KS_DISABLED) {
				pthread_mutex_lock(&g_app_mutex);
				BarUiDispatch(g_app, g_app->settings.keys[BAR_KS_BAN],
					g_app->curStation, g_app->playlist, false, BAR_DC_GLOBAL | BAR_DC_STATION | BAR_DC_SONG);
				pthread_mutex_unlock(&g_app_mutex);
				if (pd) free_post_data(pd);
				return send_json_response(connection, "{\"success\":true}", MHD_HTTP_OK);
			}
		} else if (strcmp(url, "/api/pause") == 0) {
			if (g_app && g_app->settings.keys[BAR_KS_PLAYPAUSE] != BAR_KS_DISABLED) {
				pthread_mutex_lock(&g_app_mutex);
				BarUiDispatch(g_app, g_app->settings.keys[BAR_KS_PLAYPAUSE],
					g_app->curStation, g_app->playlist, false, BAR_DC_GLOBAL | BAR_DC_STATION);
				pthread_mutex_unlock(&g_app_mutex);
				if (pd) free_post_data(pd);
				return send_json_response(connection, "{\"success\":true}", MHD_HTTP_OK);
			}
		} else if (strcmp(url, "/api/station") == 0) {
			/* Simple JSON parsing for station ID */
			/* Check if we have POST data - pd should exist for POST requests */
			if (!pd) {
				return send_json_response(connection, "{\"error\":\"POST data handler not initialized\"}", MHD_HTTP_INTERNAL_SERVER_ERROR);
			}
			/* When *upload_data_size == 0, all POST data has been received */
			/* But if pd->size is still 0, we haven't received any data yet - wait for it */
			if (pd->size == 0 || !pd->data) {
				/* No data received yet - return MHD_YES to wait for POST data */
				return MHD_YES;
			}
			
			const char *json_data = pd->data;
			const char *id_start = strstr(json_data, "\"id\"");
			if (id_start) {
				const char *colon = strchr(id_start, ':');
				if (colon) {
					const char *quote_start = strchr(colon, '"');
					if (quote_start) {
						const char *quote_end = strchr(quote_start + 1, '"');
						if (quote_end) {
							size_t id_len = quote_end - quote_start - 1;
							if (id_len > 0 && id_len < 128) {
								char station_id[128];
								memcpy(station_id, quote_start + 1, id_len);
								station_id[id_len] = '\0';
								
								if (g_app) {
									pthread_mutex_lock(&g_app_mutex);
									PianoStation_t *station = PianoFindStationById(g_app->ph.stations, station_id);
									if (station) {
										g_app->nextStation = station;
										/* Skip current song to stop playback and trigger station change */
										if (g_app->settings.keys[BAR_KS_SKIP] != BAR_KS_DISABLED) {
											BarUiDispatch(g_app, g_app->settings.keys[BAR_KS_SKIP],
												g_app->curStation, g_app->playlist, false, BAR_DC_GLOBAL | BAR_DC_STATION);
										}
										/* Clear playlist to ensure main loop fetches new playlist for next station */
										if (g_app->playlist != NULL) {
											PianoSong_t *next = PianoListNextP(g_app->playlist);
											if (next != NULL) {
												PianoDestroyPlaylist(next);
											}
											/* Free current song and set playlist to NULL */
											PianoSong_t *current = g_app->playlist;
											g_app->playlist = NULL;
											/* Note: current song will be freed by main loop or skip action */
										}
										pthread_mutex_unlock(&g_app_mutex);
										if (pd) free_post_data(pd);
										return send_json_response(connection, "{\"success\":true}", MHD_HTTP_OK);
									} else {
										pthread_mutex_unlock(&g_app_mutex);
										if (pd) free_post_data(pd);
										return send_json_response(connection, "{\"error\":\"Station not found\"}", MHD_HTTP_NOT_FOUND);
									}
								}
							}
						}
					}
				}
			}
			if (pd) free_post_data(pd);
			return send_json_response(connection, "{\"error\":\"Invalid JSON format\"}", MHD_HTTP_BAD_REQUEST);
		}
		if (pd) free_post_data(pd);
		return send_json_response(connection, "{\"error\":\"Invalid request\"}", MHD_HTTP_BAD_REQUEST);
	}
	
	if (pd) free_post_data(pd);
	struct MHD_Response *response = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
	enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
	MHD_destroy_response(response);
	return ret;
}
#endif

void BarWebInit(BarApp_t *app) {
	g_app = app;
}

void BarWebDestroy(void) {
	BarWebStop();
	g_app = NULL;
}

void BarWebStart(void) {
#ifdef HAVE_MICROHTTPD
	if (!g_app || !g_app->settings.webEnabled) {
		return;
	}
	
	if (g_daemon != NULL) {
		return; /* Already running */
	}
	
	g_daemon = MHD_start_daemon(
		MHD_USE_SELECT_INTERNALLY,
		g_app->settings.webPort,
		NULL, NULL,
		&handle_request, NULL,
		MHD_OPTION_NOTIFY_COMPLETED, &free_post_data, NULL,
		MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int) 120,
		MHD_OPTION_END);
	
	if (g_daemon != NULL) {
		BarUiMsg(&g_app->settings, MSG_INFO,
			"Web interface started on port %u\n", g_app->settings.webPort);
	} else {
		BarUiMsg(&g_app->settings, MSG_ERR,
			"Failed to start web interface on port %u\n", g_app->settings.webPort);
	}
#else
	if (g_app) {
		BarUiMsg(&g_app->settings, MSG_ERR,
			"Web interface not available (libmicrohttpd not found)\n");
	}
#endif
}

void BarWebStop(void) {
#ifdef HAVE_MICROHTTPD
	if (g_daemon != NULL) {
		MHD_stop_daemon(g_daemon);
		g_daemon = NULL;
	}
#endif
}

