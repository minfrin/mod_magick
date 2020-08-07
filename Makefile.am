EXTRA_DIST = mod_magick.c mod_magick.h mod_magick_colorspace.c mod_magick_format.c mod_magick_interlace.c mod_magick_quality.c mod_magick_resize.c mod_magick_strip.c mod_magick.spec

all-local:
	$(APXS) "-Wc,${CFLAGS}" -c -c $(DEF_LDLIBS) -Wc,"$(CFLAGS)" -Wc,"$(AM_CFLAGS)" -Wl,"$(LDFLAGS)" -Wl,"$(AM_LDFLAGS)" $(LIBS) @srcdir@/mod_magick.c
	$(APXS) "-Wc,${CFLAGS}" -c -c $(DEF_LDLIBS) -Wc,"$(CFLAGS)" -Wc,"$(AM_CFLAGS)" -Wl,"$(LDFLAGS)" -Wl,"$(AM_LDFLAGS)" $(LIBS) @srcdir@/mod_magick_colorspace.c
	$(APXS) "-Wc,${CFLAGS}" -c -c $(DEF_LDLIBS) -Wc,"$(CFLAGS)" -Wc,"$(AM_CFLAGS)" -Wl,"$(LDFLAGS)" -Wl,"$(AM_LDFLAGS)" $(LIBS) @srcdir@/mod_magick_format.c
	$(APXS) "-Wc,${CFLAGS}" -c -c $(DEF_LDLIBS) -Wc,"$(CFLAGS)" -Wc,"$(AM_CFLAGS)" -Wl,"$(LDFLAGS)" -Wl,"$(AM_LDFLAGS)" $(LIBS) @srcdir@/mod_magick_interlace.c
	$(APXS) "-Wc,${CFLAGS}" -c -c $(DEF_LDLIBS) -Wc,"$(CFLAGS)" -Wc,"$(AM_CFLAGS)" -Wl,"$(LDFLAGS)" -Wl,"$(AM_LDFLAGS)" $(LIBS) @srcdir@/mod_magick_quality.c
	$(APXS) "-Wc,${CFLAGS}" -c -c $(DEF_LDLIBS) -Wc,"$(CFLAGS)" -Wc,"$(AM_CFLAGS)" -Wl,"$(LDFLAGS)" -Wl,"$(AM_LDFLAGS)" $(LIBS) @srcdir@/mod_magick_resize.c
	$(APXS) "-Wc,${CFLAGS}" -c -c $(DEF_LDLIBS) -Wc,"$(CFLAGS)" -Wc,"$(AM_CFLAGS)" -Wl,"$(LDFLAGS)" -Wl,"$(AM_LDFLAGS)" $(LIBS) @srcdir@/mod_magick_strip.c

install-exec-local: 
	if test -z "$${LIBEXECDIR}"; then LIBEXECDIR=`$(APXS) -q LIBEXECDIR`; fi;\
	\
	mkdir -p $(DESTDIR)$${LIBEXECDIR}; \
	\
	$(APXS) "-Wc,${CFLAGS}" -S LIBEXECDIR=$(DESTDIR)$${LIBEXECDIR} -c -i -c $(DEF_LDLIBS) -Wc,"$(CFLAGS)" -Wc,"$(AM_CFLAGS)" -Wl,"$(LDFLAGS)" -Wl,"$(AM_LDFLAGS)" $(LIBS) @srcdir@/mod_magick.c; \
	$(APXS) "-Wc,${CFLAGS}" -S LIBEXECDIR=$(DESTDIR)$${LIBEXECDIR} -c -i -c $(DEF_LDLIBS) -Wc,"$(CFLAGS)" -Wc,"$(AM_CFLAGS)" -Wl,"$(LDFLAGS)" -Wl,"$(AM_LDFLAGS)" $(LIBS) @srcdir@/mod_magick_colorspace.c; \
	$(APXS) "-Wc,${CFLAGS}" -S LIBEXECDIR=$(DESTDIR)$${LIBEXECDIR} -c -i -c $(DEF_LDLIBS) -Wc,"$(CFLAGS)" -Wc,"$(AM_CFLAGS)" -Wl,"$(LDFLAGS)" -Wl,"$(AM_LDFLAGS)" $(LIBS) @srcdir@/mod_magick_format.c; \
	$(APXS) "-Wc,${CFLAGS}" -S LIBEXECDIR=$(DESTDIR)$${LIBEXECDIR} -c -i -c $(DEF_LDLIBS) -Wc,"$(CFLAGS)" -Wc,"$(AM_CFLAGS)" -Wl,"$(LDFLAGS)" -Wl,"$(AM_LDFLAGS)" $(LIBS) @srcdir@/mod_magick_interlace.c; \
	$(APXS) "-Wc,${CFLAGS}" -S LIBEXECDIR=$(DESTDIR)$${LIBEXECDIR} -c -i -c $(DEF_LDLIBS) -Wc,"$(CFLAGS)" -Wc,"$(AM_CFLAGS)" -Wl,"$(LDFLAGS)" -Wl,"$(AM_LDFLAGS)" $(LIBS) @srcdir@/mod_magick_quality.c; \
	$(APXS) "-Wc,${CFLAGS}" -S LIBEXECDIR=$(DESTDIR)$${LIBEXECDIR} -c -i -c $(DEF_LDLIBS) -Wc,"$(CFLAGS)" -Wc,"$(AM_CFLAGS)" -Wl,"$(LDFLAGS)" -Wl,"$(AM_LDFLAGS)" $(LIBS) @srcdir@/mod_magick_resize.c; \
	$(APXS) "-Wc,${CFLAGS}" -S LIBEXECDIR=$(DESTDIR)$${LIBEXECDIR} -c -i -c $(DEF_LDLIBS) -Wc,"$(CFLAGS)" -Wc,"$(AM_CFLAGS)" -Wl,"$(LDFLAGS)" -Wl,"$(AM_LDFLAGS)" $(LIBS) @srcdir@/mod_magick_strip.c

