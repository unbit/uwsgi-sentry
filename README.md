# uwsgi-sentry
uWSGI plugin for sentry integration

Features
========

The plugin allows you to send events to a sentry server using the following uWSGI subsystems:

* alarms
* hooks
* internal routing
* exception handlers

Installation
============

The plugin is uWSGI 2.0 friendly and requires libcurl development headers:

```sh
uwsgi --build-plugin https://github.com/unbit/uwsgi-sentry
```

this will result in sentry_plugin.so in the current directory (copy it to your uwsgi plugins directory or ensure to specify its absolute path in the plugin option of your configuration)

Sending alarms to sentry
========================

This is probably the core feature of the plugin.

Once loaded the plugin exposes a "sentry" alarm engine (if you do not know what a uWSGI alarm is, check here http://uwsgi-docs.readthedocs.org/en/latest/AlarmSubsystem.html)

Let's start defining an alarm for our socket listen queue

```ini
[uwsgi]
plugin = sentry

alarm = lqfull sentry:dsn=https://b70a31b3510c4cf793964a185cfe1fd0:b7d80b520139450f903720eb7991bf3d@example.com/1,logger=uwsgi.sentry,culprit=listen_queue

; raise the 'lqfull' alarm when the listen queue is full
alarm-listen-queue = lqfull

...
```

or you can track segfaults:

```ini
[uwsgi]
plugin = sentry

alarm = segfault sentry:dsn=https://b70a31b3510c4cf793964a185cfe1fd0:b7d80b520139450f903720eb7991bf3d@example.com/1,logger=uwsgi.sentry,culprit=whoknows

; raise the 'segfault' alarm on sgementation fault
alarm-segfault = segfault

...
```

or you can "track" logs:

```ini
[uwsgi]
plugin = sentry

alarm = seen sentry:dsn=https://b70a31b3510c4cf793964a185cfe1fd0:b7d80b520139450f903720eb7991bf3d@example.com/1,logger=uwsgi.sentry,culprit=whoknows

; raise the 'seen' alarm whenever a log line starts with FOO
alarm-log = seen ^FOO

...
```

And obviously you can combine it with the internal routing subsystem

```ini
[uwsgi]
plugin = sentry

; define a an alarm (named 'noputplease') when someone use the PUT method on the site
alarm = noputplease sentry:dsn=https://b70a31b3510c4cf793964a185cfe1fd0:b7d80b520139450f903720eb7991bf3d@example.com/1,logger=uwsgi.sentry

; trigger the 'noputplease' alarm when PUT method is request
route-if = isequal:${REQUEST_METHOD};PUT alarm:notputplease PUT IS NOT ALLOWED !!!
```

Triggering sentry events with hooks
===================================

uWSGI hooks (http://uwsgi-docs.readthedocs.org/en/latest/Hooks.html) are special actions that can be triggered in the various server phases.

The following example triggers a sentry event when the uWSGI instance starts and when its first worker is ready to accept requests:

```ini
[uwsgi]
plugin = sentry
hook-asap = sentry:dsn=https://b70a31b3510c4cf793964a185cfe1fd0:b7d80b520139450f903720eb7991bf3d@example.com/1,logger=uwsgi.sentry,message=uWSGI IS STARTING
hook-accepting1-once = sentry:dsn=https://b70a31b3510c4cf793964a185cfe1fd0:b7d80b520139450f903720eb7991bf3d@example.com/1,logger=uwsgi.sentry,message=uWSGI WORKER 1 IS READY
...
```

Managing Exceptions
====================

Note: this feature requires uWSGI >= 2.0.10 or to apply this patch: https://github.com/unbit/uwsgi/commit/c74428a3aa1aade1cca7e59b20c571a2a164dc13 (yeah a stupid bug)

Note again: For managing exceptions use native sentry clients, use this feature when your language is not supported by sentry or the uWSGI request plugin is not a 'language handler' (like the GlusterFS, GridFS or Rados ones) but raises errors as exceptions.

The uWSGI exception subsystem is an api exposed to plugins to have a common infrastructure for managing exceptions.

It is not required for a plugin to support it, but common language-based ones (like python and ruby) supports it.

To tell uWSGI to send exceptions to sentry use the `exception-handler` option:

```ini
[uwsgi]
plugin = sentry
exception-handler = sentry:dsn=https://b70a31b3510c4cf793964a185cfe1fd0:b7d80b520139450f903720eb7991bf3d@example.com/1,logger=uwsgi.sentry
...
```

remember: exception handlers can be stacked, just specify multiple `exception-handler` options

Advanced usage as internal routing action (dangerous)
====================================================

This is marked as "dangerous" because libcurl does a blocking session, so your request will be blocked while waiting for the sentry server to respond.

By the way, in some cases it could be a good approach. The only exposed action is 'sentry':

```ini
[uwsgi]
plugin = sentry
; trigger a sentry event when a url starting with /help is requested
route = ^/help sentry:dsn=https://b70a31b3510c4cf793964a185cfe1fd0:b7d80b520139450f903720eb7991bf3d@example.com/1,logger=uwsgi.sentry,message=help asked
...
```

Supported options
=================

The following options can be specified on all sentry handlers (check sentry docs about their meaning, http://sentry.readthedocs.org/en/latest/developer/client/) :

* dsn (required, specifies the dsn url of the sentry server)
* level (set the level of the logging, can be fatal, error, warning, info, debug)
* set the logger (example: uwsgi.sentry)
* message (set/force the message, exceptions and alarms automatically set it to their values)
* platform (the platform string)
* culprit (the guilty one ;), generally it is the name of a function or a feature)
* server_name (the name of the server, automatically set to hostname if empty)
* release (the release string)
* exception_type (the type of exception, automatically set by the exception handler)
* exception_value (the value of the exception, automatically set by the exception handler)
* no_verify (do not veryfy ssl server certificate)
* debug (enable debug mode, report HTTP transactions in the logs)
* tags (set tags, format is tags=key:value;key1:value1;key2:value2)
* extra (set extra values, format is tags=key:value;key1:value1;key2:value2)
* timeout (set http connection/response timeout, uses default socket timeout if not specified)
