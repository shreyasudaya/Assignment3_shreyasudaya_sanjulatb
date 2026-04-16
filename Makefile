CXX          = clang++
LLVM_CONFIG  = llvm-config

CXXFLAGS     = -rdynamic $(shell $(LLVM_CONFIG) --cxxflags) -fPIC -g -std=c++20
LDFLAGS      = $(shell $(LLVM_CONFIG) --ldflags | tr '\n' ' ') -Wl,--exclude-libs,ALL

BUILDDIR     = build
DEPDIR       = $(BUILDDIR)/.deps

DEPFLAGS     = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.d

TESTS        = dominator dce licm

PLUGIN_SRC   = unified.cpp
PLUGIN       = $(BUILDDIR)/unified.so

DEPFILES     = $(PLUGIN_SRC:%.cpp=$(DEPDIR)/%.d)

.PHONY: all clean tests


all: $(PLUGIN)

tests: $(TESTS:%=$(BUILDDIR)/tests/%.ll)

$(BUILDDIR)/%.o: %.cpp $(DEPDIR)/%.d | $(DEPDIR) $(BUILDDIR)
	$(CXX) $(DEPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/%.so: $(BUILDDIR)/%.o
	$(CXX) -shared $^ -o $@ $(LDFLAGS)


$(BUILDDIR)/tests/%.bc: tests/%.c | $(BUILDDIR)/tests
	clang -O0 -Xclang -disable-O0-optnone -emit-llvm -c $< -o $@

$(BUILDDIR)/tests/%-opt.bc: $(BUILDDIR)/tests/%.bc $(PLUGIN) | $(BUILDDIR)/tests
	opt -load-pass-plugin=$(PLUGIN) \
	    -passes=$* $< -o $@

$(BUILDDIR)/tests/%.ll: $(BUILDDIR)/tests/%-opt.bc | $(BUILDDIR)/tests
	llvm-dis $< -o $@

$(BUILDDIR) $(BUILDDIR)/tests $(DEPDIR):
	mkdir -p $@

clean:
	rm -rf $(BUILDDIR)

-include $(wildcard $(DEPFILES))