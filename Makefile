TARGETS := $(MAKECMDGOALS)

.PHONY: all $(TARGETS)
all: $(TARGETS)

$(TARGETS):
	mkapp -v -t $@ \
		-i $(LEGATO_ROOT)/interfaces/modemServices \
		testapp.adef

clean:
	rm -rf _build_* *.update
