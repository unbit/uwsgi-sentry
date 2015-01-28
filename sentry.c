#include <uwsgi.h>
#include <curl/curl.h>

extern struct uwsgi_server uwsgi;

/*

	Sentry integration plugin, exposes:

		alarm handler
		internal routing action
		hook
		exception handler


	alarm = foobar sentry:dns=http:///,culprit=foobar,platform=test,...

	required fields:
		dsn
		event_id
		message
		timestamp
		level (fatal, error, warning, info, debug)
		logger

	additional:
		platform
		culprit
		server_name
		release
		tags
		extra
		exception

	Authentication: X-Sentry-Auth: Sentry sentry_version=5, sentry_timestamp=1329096377,
    sentry_key=b70a31b3510c4cf793964a185cfe1fd0, sentry_client=raven-python/1.0,
    sentry_secret=b7d80b520139450f903720eb7991bf3d
		


*/

struct sentry_config {
	char *dsn;

	char *url;
	char *key;
	char *secret;

	// this is a uuid (dashes will be removed)
	char event_id[37];

	char *message;

	char *timestamp;

	char *level;

	char *logger;

	char *platform;

	char *culprit;

	char *server_name;

	char *release;

	char *no_verify;

	char *debug;
};

#define skv(x) #x, &sc->x
#define sc_free(x) if (sc->x) free(sc->x);

static int sentry_config_do(char *arg, struct sentry_config *sc) {
	if (uwsgi_kvlist_parse(arg, strlen(arg), ',', '=',
		skv(dsn),
		skv(level),
		skv(logger),
		skv(message),
		skv(platform),
		skv(culprit),
		skv(server_name),
		skv(release),
		skv(no_verify),
		skv(debug),
	NULL)) {
		uwsgi_log("[sentry] unable to parse sentry options\n");
		return -1;
	}

	if (!sc->dsn) {
		uwsgi_log("[sentry] you need to specify a dsn\n");
		return -1;
	}

	return 0 ;
}

static void sentry_config_free(struct sentry_config *sc) {
	sc_free(url);
	sc_free(key);
	sc_free(secret);

	sc_free(dsn);
        sc_free(level);
        sc_free(logger);
        sc_free(message);
        sc_free(platform);
        sc_free(culprit);
        sc_free(server_name);
        sc_free(release);
	free(sc);
}

static int sentry_dsn_parse(struct sentry_config *sc) {
	// only http and https are supported
	size_t skip = 0;
	size_t dsn_len = strlen(sc->dsn);

	if (!uwsgi_starts_with(sc->dsn, dsn_len, "http://", 7)) {
		skip = 7;
	}

	if (!uwsgi_starts_with(sc->dsn, dsn_len, "https://", 8)) {
                skip = 8;
        }

	// find the @
	char *at = strchr(sc->dsn+skip, '@');
	if (!at) goto error;

	// find the last slash
	// does the dsn ends with a slash ?
	int ends_with_a_slash = 0;
	if (sc->dsn[dsn_len-1] == '/') {
		ends_with_a_slash = 1;
		sc->dsn[dsn_len-1] = 0;
	}

	char *last_slash = strrchr(at+1, '/');
	if (ends_with_a_slash) sc->dsn[dsn_len-1] = '/';
	if (!last_slash) goto error;

	size_t base_len = last_slash - (at+1);
	char *base = uwsgi_concat4n(sc->dsn, skip, at+1, base_len, "/api/", 5, last_slash+1, strlen(last_slash+1));
	// append /store
	if (ends_with_a_slash) {
		sc->url = uwsgi_concat2(base, "store/");
	}
	else {
		sc->url = uwsgi_concat2(base, "/store/");
	}
	free(base);

	// now manage the auth part
	char *auth = sc->dsn+skip;
	size_t auth_len = at-auth;

	// find colon
	char *colon = memchr(auth, ':', auth_len);
	if (!colon) goto error;

	sc->key = uwsgi_concat2n(auth, colon-auth, "", 0);
	sc->secret = uwsgi_concat2n(colon+1, at-(colon+1), "", 0);

	uwsgi_log("[sentry] parsed url: %s\n", sc->url);
	return 0;
error:
	uwsgi_log("[sentry] unable to parse dsn: %s\n", sc->dsn); 
        return -1;
}

static int sentry_exception_handler(struct uwsgi_exception_handler_instance *uehi, char *buf, size_t len) {
	uwsgi_log("ARG = %s\n", uehi->arg);
        //uwsgi_hooked_parse(buf, len, uwsgi_exception_handler_log_parser, NULL);
        return 0;
}

static size_t sentry_curl_writefunc(void *ptr, size_t size, size_t nmemb, void *data) {
	struct sentry_config *sc = (struct sentry_config *) data;
	size_t len = size*nmemb;
	if (sc->debug) {
		uwsgi_log("%.*s\n", len, (char *)ptr);
	}
	return len;
}

static void sentry_request(struct sentry_config *sc, char *msg, size_t len) {
	CURL *curl = curl_easy_init();	
	if (!curl) return;

	struct uwsgi_buffer *ub_auth = NULL, *ub_body = NULL;
	struct curl_slist *headers = NULL, *header = NULL;

	time_t now = uwsgi_now();

	int timeout = uwsgi.socket_timeout;
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, timeout);
	curl_easy_setopt(curl, CURLOPT_URL, sc->url);

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, sentry_curl_writefunc);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, sc);

	if (sc->no_verify) {
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	}

	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");

	// we need a uwsgi buffer to build the X-Sentry-Auth header
	ub_auth = uwsgi_buffer_new(uwsgi.page_size);
	if (uwsgi_buffer_append(ub_auth, "X-Sentry-Auth: Sentry sentry_version=5, sentry_timestamp=", 57)) goto end;;
	if (uwsgi_buffer_num64(ub_auth, now)) goto end;
	if (uwsgi_buffer_append(ub_auth, ", sentry_key=", 13)) goto end;
	if (uwsgi_buffer_append(ub_auth, sc->key, strlen(sc->key))) goto end;
	if (uwsgi_buffer_append(ub_auth, ", sentry_client=uwsgi-sentry, sentry_secret=", 44)) goto end;
	if (uwsgi_buffer_append(ub_auth, sc->secret, strlen(sc->secret))) goto end;
	// null ending
	if (uwsgi_buffer_append(ub_auth, "\0", 1)) goto end;

	header = curl_slist_append(headers, ub_auth->buf);
	if (!header) goto end;
	headers = header;

	header = curl_slist_append(headers, "Content-Type: application/json");
	if (!header) goto end;
        headers = header;

	// disable Expect header
	header = curl_slist_append(headers, "Expect: ");
	if (!header) goto end;
        headers = header;

	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	// prepare the json
	ub_body = uwsgi_buffer_new(uwsgi.page_size);
	// event_id, unfortunately we need to remove dashed from the uwsgi-generated uuid
	char uuid[37];
	uwsgi_uuid(uuid);
	if (uwsgi_buffer_append(ub_body, "{\"event_id\":\"", 13)) goto end;
	if (uwsgi_buffer_append(ub_body, uuid, 8)) goto end;
	if (uwsgi_buffer_append(ub_body, uuid+9, 4)) goto end;
	if (uwsgi_buffer_append(ub_body, uuid+14, 4)) goto end;
	if (uwsgi_buffer_append(ub_body, uuid+19, 4)) goto end;
	if (uwsgi_buffer_append(ub_body, uuid+24, 12)) goto end;
	if (uwsgi_buffer_append(ub_body, "\"", 1)) goto end;

	if (sc->level) {
		if (uwsgi_buffer_append(ub_body, ",\"level\":\"", 10)) goto end;
		if (uwsgi_buffer_append_json(ub_body, sc->level, strlen(sc->level))) goto end;
		if (uwsgi_buffer_append(ub_body, "\"", 1)) goto end;
	}

	if (sc->logger) {
		if (uwsgi_buffer_append(ub_body, ",\"logger\":\"", 11)) goto end;
		if (uwsgi_buffer_append_json(ub_body, sc->logger, strlen(sc->logger))) goto end;
		if (uwsgi_buffer_append(ub_body, "\"", 1)) goto end;
	}

	if (sc->culprit) {
		if (uwsgi_buffer_append(ub_body, ",\"culprit\":\"", 12)) goto end;
		if (uwsgi_buffer_append_json(ub_body, sc->culprit, strlen(sc->culprit))) goto end;
		if (uwsgi_buffer_append(ub_body, "\"", 1)) goto end;
	}

	if (sc->platform) {
		if (uwsgi_buffer_append(ub_body, ",\"platform\":\"", 13)) goto end;
		if (uwsgi_buffer_append_json(ub_body, sc->platform, strlen(sc->platform))) goto end;
		if (uwsgi_buffer_append(ub_body, "\"", 1)) goto end;
	}

	if (sc->release) {
		if (uwsgi_buffer_append(ub_body, ",\"release\":\"", 12)) goto end;
		if (uwsgi_buffer_append_json(ub_body, sc->release, strlen(sc->release))) goto end;
		if (uwsgi_buffer_append(ub_body, "\"", 1)) goto end;
	}

	if (uwsgi_buffer_append(ub_body, ",\"server_name\":\"", 16)) goto end;
	if (sc->server_name) {
		if (uwsgi_buffer_append_json(ub_body, sc->release, strlen(sc->release))) goto end;
	}
	else {
		if (uwsgi_buffer_append_json(ub_body, uwsgi.hostname, uwsgi.hostname_len)) goto end;
	}

	if (uwsgi_buffer_append(ub_body, "\",\"timestamp\":\"", 15)) goto end;
	char tm[sizeof("0000-00-00T00:00:00")];
	strftime(tm, sizeof(tm), "%FT%T", gmtime(&now));
	if (uwsgi_buffer_append(ub_body, tm, strlen(tm))) goto end;

	if (uwsgi_buffer_append(ub_body, "\",\"message\":\"", 13)) goto end;
	if (sc->message) {
		if (uwsgi_buffer_append_json(ub_body, sc->message, strlen(sc->message))) goto end;
	}
	else {
		if (uwsgi_buffer_append_json(ub_body, msg, len)) goto end;
	}
	if (uwsgi_buffer_append(ub_body, "\"}", 2)) goto end;


	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, ub_body->buf);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, ub_body->pos);

	if (sc->debug) {
		uwsgi_log("[sentry] sending %.*s to %s\n",  ub_body->pos, ub_body->buf, sc->url);
	}

	CURLcode res = curl_easy_perform(curl);

	if (res != CURLE_OK) {
		uwsgi_log_verbose("[sentry] error sending request: %.*s\n", ub_body->pos, ub_body->buf);
	}
	else {
		long http_code = 0;
#ifdef CURLINFO_RESPONSE_CODE
        	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
#else
        	curl_easy_getinfo(curl, CURLINFO_HTTP_CODE, &http_code);
#endif
        	if (http_code != 200) {
                	uwsgi_log_verbose("[sentry] HTTP api returned non-200 response code: %d\n", (int) http_code);
        	}
	}

end:
	if (headers) curl_slist_free_all(headers);
	if (ub_auth) uwsgi_buffer_destroy(ub_auth);
	curl_easy_cleanup(curl);
}

static void sentry_alarm_func(struct uwsgi_alarm_instance *uai, char *msg, size_t len) {
	struct sentry_config *sc = (struct sentry_config *) uai->data_ptr;
	sentry_request(sc, msg, len);
}

static void sentry_alarm_init(struct uwsgi_alarm_instance *uai) {
	struct sentry_config *sc = uwsgi_calloc(sizeof(struct sentry_config));
	if (sentry_config_do(uai->arg, sc)) {
		// useless :P
		sentry_config_free(sc);
		exit(1);
	}

	if (sentry_dsn_parse(sc)) {
		// useless again :P
		sentry_config_free(sc);
		exit(1);
	}
	uai->data_ptr = sc;
}

static int sentry_hook(char *arg) {
	int ret = -1;
	struct sentry_config *sc = uwsgi_calloc(sizeof(struct sentry_config));
	if (sentry_config_do(arg, sc)) {
		goto end;
        }

        if (sentry_dsn_parse(sc)) {
		goto end;
        }

	// empty messae in case sc->message is not defined
	// we do not check for errors here as we do nto want to destroy the
	// instace if the sentry server is down
	sentry_request(sc, "", 0);
	ret = 0;
end:
	sentry_config_free(sc);
	return ret;
}

static void sentry_register() {
	uwsgi_register_exception_handler("sentry", sentry_exception_handler);
	uwsgi_register_alarm("sentry", sentry_alarm_init, sentry_alarm_func);
	uwsgi_register_hook("sentry", sentry_hook);
}

struct uwsgi_plugin sentry_plugin = {
	.name = "sentry",
	.on_load = sentry_register,
};
