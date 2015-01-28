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

uWSGI hooks (http://uwsgi-docs.readthedocs.org/en/latest/Hooks.html) are special action that can be triggered in the various server phases.

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

Note: this features requires uWSGI >= 2.0.10 or to apply this patch: https://github.com/unbit/uwsgi/commit/c74428a3aa1aade1cca7e59b20c571a2a164dc13 (yeah a stupid bug)

Note again: For managing exceptions use native sentry clients, uset this feature when your language is not supported by sentry or the uWSGI request plugin is not a 'language handler' (like the GlusterFS, GridFS or Rados ones) but raises errors as exceptions.

The uWSGI exception system

Advanced usage as internal routingaction (dangerous)
====================================================

Supported options
=================
