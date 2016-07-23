p_NAME := find_minmax
p_CSRCS := $(wildcard *.c)
p_COBJS := ${p_CSRCS:.c=.o}
p_OBJS := $(p_COBJS)
p_INCDIRS := $(wildcard inc/*.h)
p_LIBDIRS :=
p_LIBRARIES :=

CPPFLAGS += $(foreach incdir, $(p_INCDIRS),-I$(incdir))
LDFLAGS += $(foreach libdir,$(p_LIBDIRS),-L$(libdir))
LDFLAGS += $(foreach lib,$(p_LIBRARIES),-l$(lib))

.PHONY: all clean distclean

all: $(p_NAME)

$(p_NAME): $(p_OBJS)
	$(LINK.c) $(p_OBJS) -pthread -lrt -g -o $(p_NAME)

clean:
	@- $(RM) $(p_NAME)
	@- $(RM) $(p_OBJS)

distclean: clean

define OBJ_DEP_HEADERS
 $(1) : ${1:.o}
endef
$(foreach objf,$(p_OBJS),$(eval $(call OBJ_DEP_HEADERS,$(objf))))
