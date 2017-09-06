all: glxapi.c glapi.c eglapi.c

%api.c: %.xml genapis.py reg.py
	$(srcdir)/genapis.py $(srcdir)

