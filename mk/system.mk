# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

# Text must fit segment 0x30; data and BSS must end below physical 0x0A0000 (TPA).
LDSYS = -i -L
# splitent.o must link first for its interface at offset zero.
# The module resolves independently; splitimg.o embeds its contiguous text/data image.
$(SPLITMOD): $(MODOBJ)
	$(LD) -L -e spemu_ -R 0x37000000 -o $@ $(MODOBJ) > $(OBJDIR)/modlink.txt 2>&1 \
	    || { echo "*** split.mod: the module is linked BEFORE cpm.sys and knows"; \
	         echo "*** no cpm.sys address, so it has to resolve entirely among"; \
	         echo "*** its own objects.  Either add the callee to MODSRC or"; \
	         echo "*** reach it through the interface header (src/bdos/splitent.s)."; \
	         cat $(OBJDIR)/modlink.txt; exit 1; }
	@test ! -s $(OBJDIR)/modlink.txt || { cat $(OBJDIR)/modlink.txt; exit 1; }

$(OBJDIR)/splitimg.s: $(SPLITMOD) tools/mkblob.py src/bios/c900cfg.h
	python3 tools/mkblob.py $(SPLITMOD) $@

$(OBJDIR)/splitimg.o: $(OBJDIR)/splitimg.s
	$(AS) -g -o $@ $< >> $(LOG) 2>&1

$(CPMSYS): $(OBJ)
	$(LD) $(LDSYS) -e start -R 0x30000000 -o $@ $(OBJ) > $(OBJDIR)/link.txt 2>&1
	@python3 -c 'import struct,sys; \
b = open("$(CPMSYS)","rb").read(48); \
gl = lambda o: (struct.unpack_from("<H",b,o)[0]<<16)|struct.unpack_from("<H",b,o+2)[0]; \
text = gl(8)+gl(12); data = gl(20)+gl(24); bss = gl(28); \
print("cpm.sys: text=0x%x data=0x%x bss=0x%x" % (text,data,bss)); \
rnd = (text + 1023) // 1024 * 1024; \
free = 0x20000 - (rnd + data + bss); \
print("cpm.sys: %d bytes free below phys 0x0A0000" % free); \
sys.exit("TEXT > 64K: data seg moves to 0x32 and collides with the TPA" if text > 0x10000 else \
("data+bss > 64K" if data+bss > 0x10000 else \
("IMAGE OVERRUNS phys 0x0A0000 (the TPA) by %d bytes: crt.s puts data at 0x080000+roundup(text,1K), so roundup(text,1K)+data+bss must stay under 128K.  Past it the image runs into the transient program area." % -free) if free < 0 else None))'

# cc0 -> cc1 -> cc2.  0012 = VPEEP+VKERN: frame refs in SS=0x3F, matching
# the ROM routines the BIOS calls (console, wdread).
$(OBJDIR)/%.o: src/bios/%.c $(SRCHDRS) $(TCSTAMP) | $(OBJDIR)
	$(CC0) $(VAR) $< $(OBJDIR)/$*.z0 $(SRCINC) $(DEFS) >> $(LOG) 2>&1
	$(CC1) $(VAR) $(OBJDIR)/$*.z0 $(OBJDIR)/$*.z1 >> $(LOG) 2>&1
	$(CC2) 0012 $(OBJDIR)/$*.z1 $@ $(OBJDIR)/$*.scr 0 >> $(LOG) 2>&1

$(OBJDIR)/%.o: src/bdos/%.c $(SYSHDRS) $(TCSTAMP) | $(OBJDIR)
	$(CC0) $(VAR) $< $(OBJDIR)/$*.z0 $(SYSINC) $(DEFS) >> $(LOG) 2>&1
	$(CC1) $(VAR) $(OBJDIR)/$*.z0 $(OBJDIR)/$*.z1 >> $(LOG) 2>&1
	$(CC2) 0012 $(OBJDIR)/$*.z1 $@ $(OBJDIR)/$*.scr 0 >> $(LOG) 2>&1

$(OBJDIR)/%.o: src/ccp/%.c $(SYSHDRS) $(TCSTAMP) | $(OBJDIR)
	$(CC0) $(VAR) $< $(OBJDIR)/$*.z0 $(SYSINC) $(DEFS) >> $(LOG) 2>&1
	$(CC1) $(VAR) $(OBJDIR)/$*.z0 $(OBJDIR)/$*.z1 >> $(LOG) 2>&1
	$(CC2) 0012 $(OBJDIR)/$*.z1 $@ $(OBJDIR)/$*.scr 0 >> $(LOG) 2>&1

# asm: cpp then as
$(OBJDIR)/%.o: src/bios/%.s $(SRCHDRS) $(TCSTAMP) | $(OBJDIR)
	cpp -traditional-cpp -P $(DEFS) $(SRCINC) $< > $(OBJDIR)/$*.i 2>> $(LOG)
	$(AS) -g -o $@ $(OBJDIR)/$*.i >> $(LOG) 2>&1

$(OBJDIR)/%.o: src/bdos/%.s $(SYSHDRS) $(TCSTAMP) | $(OBJDIR)
	cpp -traditional-cpp -P $(DEFS) $(SYSINC) $< > $(OBJDIR)/$*.i 2>> $(LOG)
	$(AS) -g -o $@ $(OBJDIR)/$*.i >> $(LOG) 2>&1

$(OBJDIR)/%.o: src/ccp/%.s $(SYSHDRS) $(TCSTAMP) | $(OBJDIR)
	cpp -traditional-cpp -P $(DEFS) $(SYSINC) $< > $(OBJDIR)/$*.i 2>> $(LOG)
	$(AS) -g -o $@ $(OBJDIR)/$*.i >> $(LOG) 2>&1

$(OBJDIR):
	mkdir -p $(OBJDIR)
	: > $(LOG)

# Refresh the compiler stamp every build, preserving its mtime if unchanged.
$(TCSTAMP): FORCE | $(OBJDIR)
	@sh tools/tcstamp.sh '$(C900_TOOLCHAIN)' > $@$(TMPSFX)
	@cat $@$(TMPSFX); cat $@$(TMPSFX) >> $(LOG)
	@cmp -s $@$(TMPSFX) $@ || cp $@$(TMPSFX) $@
	@rm -f $@$(TMPSFX)
