# https://stackoverflow.com/questions/8096015/creating-a-simple-makefile-to-build-a-shared-library
# https://stackoverflow.com/questions/48791883/best-practice-for-building-a-make-file

all : # The canonical default target.
COMPILER=gcc
BUILD := debug
include_dir := include
source_dir := src
build_dir := build/${BUILD}
exes := # Executables to build.
libraries := # Executables to build.

library_test_prefix := testlib_
library_tests_sources := $(shell find ${source_dir} -type f -name "${library_test_prefix}*.c")
library_tests := ${library_tests_sources:${source_dir}/%.c=%}
-include ${library_tests:%=${build_dir}/%.d}

# ==== Exec1
# exes += amopt_test
# objects.amopt_test = main.o libam_log.o libam_opts.o
# -include ${objects.amopt_test:%.o=${build_dir}/%.d} # Include auto-generated dependencies.

# ==== library
libraries += libam
objects.libam = libam_cqueue.o  libam_fdopers.o  libam_time.o libam_log.o  libam_opts.o  libam_stats.o  libam_tree.o libam_itree.o libam_hash.o libam_stack.o libam_lstack.o libam_pool.o
-include ${objects.libam:%.o=${build_dir}/%.d} # Include auto-generated dependencies.

CXX.gcc := g++
CC.gcc := gcc
LD.gcc := gcc
AR.gcc := ar

CXX.clang := clang++
CC.clang := clang
LD.clang := clang++
AR.clang := ar

CXX := ${CXX.${COMPILER}}
CC := ${CC.${COMPILER}}
LD := ${LD.${COMPILER}}
AR := ${AR.${COMPILER}}

CFLAGS.gcc.all := -std=gnu17 -Wall -Wextra -Werror -I ${include_dir} -pthread
CFLAGS.gcc.debug := -g -Og -DDEBUG
CFLAGS.gcc.release := -O3 -DNDEBUG
CFLAGS.gcc := ${CFLAGS.gcc.all} ${CFLAGS.gcc.${BUILD}}

CFLAGS.clang.all := -std=gnu17 -W{all,extra,error} -I ${include_dir} -pthread
CFLAGS.clang.debug := -g -Og -DDEBUG
CFLAGS.clang.release := -O3 -DNDEBUG
CFLAGS.clang := ${CFLAGS.clang.all} ${CFLAGS.clang.${BUILD}}

CXXFLAGS.gcc.all := -std=gnu++14 -Wall -Wextra -Werror -I ${include_dir} -pthread
CXXFLAGS.gcc.debug := -g -Og -DDEBUG
CXXFLAGS.gcc.release := -O3 -DNDEBUG
CXXFLAGS.gcc := ${CXXFLAGS.gcc.all} ${CXXFLAGS.gcc.${BUILD}}

CXXFLAGS.clang.all := -std=gnu++14 -Wall -Wextra -Werror -I ${include_dir} -pthread
CXXFLAGS.clang.debug := -g -Og -DDEBUG
CXXFLAGS.clang.release := -O3 -DNDEBUG
CXXFLAGS.clang := ${CXXFLAGS.clang.all} ${CXXFLAGS.clang.${BUILD}}

CXXFLAGS := ${CXXFLAGS.${COMPILER}}
CFLAGS := ${CFLAGS.${COMPILER}}

LDFLAGS.debug := -g
LDFLAGS.release :=
LDFLAGS := ${LDFLAGS.${BUILD}}
LDLIBS := -lpthread

COMPILE.CXX = ${CXX} -c -o $@ ${CPPFLAGS} -MD -MP ${CXXFLAGS} $(abspath $<)
PREPROCESS.CXX = ${CXX} -E -o $@ ${CPPFLAGS} ${CXXFLAGS} $(abspath $<)
COMPILE.C = ${CC} -fpic -c -o $@ ${CPPFLAGS} -MD -MP ${CFLAGS} $(abspath $<)
LINK.EXE = ${LD} -o $@ $(LDFLAGS) $(filter-out makefile,$^) $(LDLIBS)
LINK.SO = ${LD} -fpic -shared -o $@.so $(LDFLAGS) $(filter-out makefile,$^) $(LDLIBS)
LINK.A = ${AR} -rcs $@.a $(filter-out makefile,$^)

all : ${build_dir} ${libraries:%=${build_dir}/%} ${exes:%=${build_dir}/%}

test : ${build_dir} ${libraries:%=${build_dir}/%} ${exes:%=${build_dir}/%} ${library_tests:%=${build_dir}/%} ${library_tests:%=${build_dir}/run_%}

tests : test

check : test

.SECONDEXPANSION:

${exes:%=${build_dir}/%} : ${build_dir}/% : $$(addprefix ${build_dir}/, $${objects.$$*}) makefile | ${build_dir}
	$(strip ${LINK.EXE})

${libraries:%=${build_dir}/%} : ${build_dir}/% : $$(addprefix ${build_dir}/, $${objects.$$*}) makefile | ${build_dir}
	$(strip ${LINK.SO})
	$(strip ${LINK.A})
	touch $@
	ln -sf $@.so
	ln -sf $@.a

${library_tests:%=${build_dir}/%} : % : $$(addsuffix .o, $${*}) makefile ${libraries:%=${build_dir}/%.a} | ${build_dir}
	$(strip ${LINK.EXE} ${libraries:%=${build_dir}/%.a} -lm)

# Create the build directory on demand.
${build_dir} :
	mkdir -p $@

# Compile a C++ source into .o.
# Most importantly, generate header dependencies.
#${build_dir}/%.o : %.cc makefile | ${build_dir}
#	$(strip ${COMPILE.CXX})

# Compile a C source into .o.
# Most importantly, generate header dependencies.
${build_dir}/%.o : ${source_dir}/%.c makefile | ${build_dir}
	$(strip ${COMPILE.C})

${build_dir}/run_${library_test_prefix}% : ${build_dir}/${library_test_prefix}% makefile | ${build_dir}
	${@:${build_dir}/run_%=${build_dir}/%}
	touch $@

clean :
	rm -rf ${build_dir}

.PHONY : all

