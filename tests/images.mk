# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

# Separate images isolate test fixtures from the development disk.
CPMACONC = build/cpma-conc.img
DISKAC   = build/diska-conc
# concpad.sh places CONCTGT.TXT entries in different directory records.
$(CPMACONC): $(CPMAIMG) $(UCONCFS) tools/mkcpmfs.py tools/sparse.py tests/concpad.sh | $(OBJDIR)
	@rm -rf $(DISKAC)
	@mkdir -p $(DISKAC)
	@for f in $(DISKA)/*; do b=`basename $$f`; \
		cmp -s $$f $(DISKAC)/$$b || cp $$f $(DISKAC)/$$b; done
	@for f in $(UCONCFS); do b=`basename $$f`; \
		cmp -s $$f $(DISKAC)/$$b || cp $$f $(DISKAC)/$$b; done
	sh tests/concpad.sh $(DISKAC) $(CPMA_BLOCKS) $(LABEL) $(LABELMODE)
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $@ $(CPMA_BLOCKS) $(DISKAC)

# XDOS and console fixtures.
CPMAXDOS = build/cpma-xdos.img
DISKAX   = build/diska-xdos
$(CPMAXDOS): $(CPMAIMG) $(UXDOS) $(UCON1) tools/mkcpmfs.py tools/sparse.py | $(OBJDIR)
	@rm -rf $(DISKAX)
	@mkdir -p $(DISKAX)
	@for f in $(DISKA)/*; do b=`basename $$f`; \
		cmp -s $$f $(DISKAX)/$$b || cp $$f $(DISKAX)/$$b; done
	@for f in $(UXDOS) $(UCON1); do b=`basename $$f`; \
		cmp -s $$f $(DISKAX)/$$b || cp $$f $(DISKAX)/$$b; done
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $@ $(CPMA_BLOCKS) $(DISKAX)

# Both idle-console cost runs boot this same image.
CPMACONCZ = build/cpma-concz.img
DISKAZ    = build/diska-concz
$(CPMACONCZ): $(CPMAIMG) $(UCONCZ) tools/mkcpmfs.py tools/sparse.py | $(OBJDIR)
	@rm -rf $(DISKAZ)
	@mkdir -p $(DISKAZ)
	@for f in $(DISKA)/*; do b=`basename $$f`; \
		cmp -s $$f $(DISKAZ)/$$b || cp $$f $(DISKAZ)/$$b; done
	@for f in $(UCONCZ); do b=`basename $$f`; \
		cmp -s $$f $(DISKAZ)/$$b || cp $$f $(DISKAZ)/$$b; done
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $@ $(CPMA_BLOCKS) $(DISKAZ)

# Poll cost and wake tests share this image.
CPMAXDOSPOL = build/cpma-xdospol.img
DISKAXP     = build/diska-xdospol
# CATT detaches the console owner so XDOSPOL can read console 1.
$(CPMAXDOSPOL): $(CPMAIMG) $(UXDOSPOL) $(UOBJDIR)/CATT.Z8K tools/mkcpmfs.py tools/sparse.py | $(OBJDIR)
	@rm -rf $(DISKAXP)
	@mkdir -p $(DISKAXP)
	@for f in $(DISKA)/*; do b=`basename $$f`; \
		cmp -s $$f $(DISKAXP)/$$b || cp $$f $(DISKAXP)/$$b; done
	@for f in $(UXDOSPOL) $(UOBJDIR)/CATT.Z8K; do b=`basename $$f`; \
		cmp -s $$f $(DISKAXP)/$$b || cp $$f $(DISKAXP)/$$b; done
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $@ $(CPMA_BLOCKS) $(DISKAXP)
# System-mode scheduling fixtures.
CPMACONCS = build/cpma-concs.img
DISKAS    = build/diska-concs
$(CPMACONCS): $(CPMAIMG) $(UCONCS) tools/mkcpmfs.py tools/sparse.py | $(OBJDIR)
	@rm -rf $(DISKAS)
	@mkdir -p $(DISKAS)
	@for f in $(DISKA)/*; do b=`basename $$f`; \
		cmp -s $$f $(DISKAS)/$$b || cp $$f $(DISKAS)/$$b; done
	@for f in $(UCONCS); do b=`basename $$f`; \
		cmp -s $$f $(DISKAS)/$$b || cp $$f $(DISKAS)/$$b; done
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $@ $(CPMA_BLOCKS) $(DISKAS)

# PROFILE.SUB must be absent from the baseline image for the cold-start comparison.
CPMAPROF = build/cpma-prof.img
DISKAP   = build/diska-prof
$(CPMAPROF): $(CPMAIMG) $(wildcard src/dist/disk-a-prof/*) \
		tools/mkcpmfs.py tools/sparse.py | $(OBJDIR)
	@rm -rf $(DISKAP)
	@mkdir -p $(DISKAP)
	@for f in $(DISKA)/*; do b=`basename $$f`; \
		cmp -s $$f $(DISKAP)/$$b || cp $$f $(DISKAP)/$$b; done
	@for f in src/dist/disk-a-prof/*; do b=`basename $$f`; \
		cmp -s $$f $(DISKAP)/$$b || cp $$f $(DISKAP)/$$b; done
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $@ $(CPMA_BLOCKS) $(DISKAP)

# Keep GET/PUT command scripts off the shared development image.
CPMAGP = build/cpma-gp.img
DISKAG = build/diska-gp
$(CPMAGP): $(CPMAIMG) $(UGP) $(wildcard src/dist/disk-a-gp/*) \
		tools/mkcpmfs.py tools/sparse.py | $(OBJDIR)
	@rm -rf $(DISKAG)
	@mkdir -p $(DISKAG)
	@# $(APPBIN) too: verify-rsxn builds apps after `all' packed drive A.
	@for f in $(DISKA)/* $(APPBIN); do b=`basename $$f`; \
		[ -f $$f ] || continue; \
		cmp -s $$f $(DISKAG)/$$b || cp $$f $(DISKAG)/$$b; done
	@for f in $(UGP) src/dist/disk-a-gp/*; do b=`basename $$f`; \
		cmp -s $$f $(DISKAG)/$$b || cp $$f $(DISKAG)/$$b; done
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $@ $(CPMA_BLOCKS) $(DISKAG)

# Warm-boot segment ownership fixture (F5).
CPMACONCW = build/cpma-concw.img
DISKACW   = build/diska-concw
$(CPMACONCW): $(CPMAIMG) $(UCONCW) tools/mkcpmfs.py tools/sparse.py | $(OBJDIR)
	@rm -rf $(DISKACW)
	@mkdir -p $(DISKACW)
	@for f in $(DISKA)/*; do b=`basename $$f`; \
		cmp -s $$f $(DISKACW)/$$b || cp $$f $(DISKACW)/$$b; done
	@for f in $(UCONCW); do b=`basename $$f`; \
		cmp -s $$f $(DISKACW)/$$b || cp $$f $(DISKACW)/$$b; done
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $@ $(CPMA_BLOCKS) $(DISKACW)

# Malformed x.out fixtures for verify-xout.  tests/mkxout.py derives them
# from MHELLO.Z8K, so a fixture that fails to be malformed in exactly the
# intended way fails the script rather than quietly testing nothing.
CPMAXOUT = build/cpma-xout.img
DISKAXO  = build/diska-xout
$(CPMAXOUT): $(CPMAIMG) $(UOBJDIR)/MHELLO.Z8K $(UXDMA) tests/mkxout.py \
		tools/mkcpmfs.py tools/sparse.py | $(OBJDIR)
	@rm -rf $(DISKAXO)
	@mkdir -p $(DISKAXO)
	@for f in $(DISKA)/*; do b=`basename $$f`; \
		cmp -s $$f $(DISKAXO)/$$b || cp $$f $(DISKAXO)/$$b; done
	@for f in $(UXDMA); do b=`basename $$f`; \
		cmp -s $$f $(DISKAXO)/$$b || cp $$f $(DISKAXO)/$$b; done
	python3 tests/mkxout.py $(UOBJDIR)/MHELLO.Z8K $(DISKAXO)
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $@ $(CPMA_BLOCKS) $(DISKAXO)

# F4 descriptor-reservation fixtures: the development medium plus the two
# creators.  Their own image rather than $(CPMACONC), which several
# concurrency targets share and none of them expects to grow.
CPMACONCR = build/cpma-concr.img
DISKACR   = build/diska-concr
$(CPMACONCR): $(CPMAIMG) $(UCONCR) tools/mkcpmfs.py tools/sparse.py | $(OBJDIR)
	@rm -rf $(DISKACR)
	@mkdir -p $(DISKACR)
	@for f in $(DISKA)/*; do b=`basename $$f`; \
		cmp -s $$f $(DISKACR)/$$b || cp $$f $(DISKACR)/$$b; done
	@for f in $(UCONCR); do b=`basename $$f`; \
		cmp -s $$f $(DISKACR)/$$b || cp $$f $(DISKACR)/$$b; done
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $@ $(CPMA_BLOCKS) $(DISKACR)

# F14 fixtures for the DISCRIMINATING descriptor race: the development
# medium plus CONCL and CONCR.  Its own image for the same reason
# $(CPMACONCR) has one -- a shared image none of its users expects to grow.
CPMACONCR2 = build/cpma-concr2.img
DISKACR2   = build/diska-concr2
$(CPMACONCR2): $(CPMAIMG) $(UCONCR2) tools/mkcpmfs.py tools/sparse.py | $(OBJDIR)
	@rm -rf $(DISKACR2)
	@mkdir -p $(DISKACR2)
	@for f in $(DISKA)/*; do b=`basename $$f`; \
		cmp -s $$f $(DISKACR2)/$$b || cp $$f $(DISKACR2)/$$b; done
	@for f in $(UCONCR2); do b=`basename $$f`; \
		cmp -s $$f $(DISKACR2)/$$b || cp $$f $(DISKACR2)/$$b; done
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $@ $(CPMA_BLOCKS) $(DISKACR2)

# F4 split-I/D fixtures: the development medium plus SPLITB, which drives
# the release disk's own 0xEE0B tools (ASZ8K and SIZEZ8K) and the source
# file MINI.8KN that is already staged there.
CPMASPLIT = build/cpma-split.img
DISKASP   = build/diska-split
$(CPMASPLIT): $(CPMAIMG) $(USPLIT) tools/mkcpmfs.py tools/sparse.py | $(OBJDIR)
	@rm -rf $(DISKASP)
	@mkdir -p $(DISKASP)
	@for f in $(DISKA)/*; do b=`basename $$f`; \
		cmp -s $$f $(DISKASP)/$$b || cp $$f $(DISKASP)/$$b; done
	@for f in $(USPLIT); do b=`basename $$f`; \
		cmp -s $$f $(DISKASP)/$$b || cp $$f $(DISKASP)/$$b; done
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $@ $(CPMA_BLOCKS) $(DISKASP)

# Quantum measurement fixtures.
CPMACONCV = build/cpma-concv.img
DISKACV   = build/diska-concv
$(CPMACONCV): $(CPMAIMG) $(UCONCV) tools/mkcpmfs.py tools/sparse.py | $(OBJDIR)
	@rm -rf $(DISKACV)
	@mkdir -p $(DISKACV)
	@for f in $(DISKA)/*; do b=`basename $$f`; \
		cmp -s $$f $(DISKACV)/$$b || cp $$f $(DISKACV)/$$b; done
	@for f in $(UCONCV); do b=`basename $$f`; \
		cmp -s $$f $(DISKACV)/$$b || cp $$f $(DISKACV)/$$b; done
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $@ $(CPMA_BLOCKS) $(DISKACV)

# F1 directory-guard fixtures.  DGENA/DGENB/DERR need nothing of the medium
# (verify-dirwerr truncates the whole disk, not this partition).
CPMADIRG = build/cpma-dirg.img
DISKADG  = build/diska-dirg
$(CPMADIRG): $(CPMAIMG) $(UDIRG) tools/mkcpmfs.py tools/sparse.py | $(OBJDIR)
	@rm -rf $(DISKADG)
	@mkdir -p $(DISKADG)
	@for f in $(DISKA)/*; do b=`basename $$f`; \
		cmp -s $$f $(DISKADG)/$$b || cp $$f $(DISKADG)/$$b; done
	@for f in $(UDIRG); do b=`basename $$f`; \
		cmp -s $$f $(DISKADG)/$$b || cp $$f $(DISKADG)/$$b; done
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $@ $(CPMA_BLOCKS) $(DISKADG)

# THE CORRUPT ENTRY IS THE FIXTURE.  dirpoke.py writes one out-of-range
# block number into one A: directory entry after the file system is built,
# and fails if it cannot find an entry to write it into -- so verify-dirbnd
# cannot pass by being handed a clean disk.  2600 is past A:'s dsm (2559)
# and lands, byte 325 of the shared pool, inside B:'s live allocation map
# (src/bios/bios900.c drvinit carves both out of one alvpool[]).
DIRBNDBLK = 2600
CPMADIRB = build/cpma-dirb.img
DISKADB  = build/diska-dirb
$(CPMADIRB): $(CPMAIMG) $(UDIRB) tools/mkcpmfs.py tools/sparse.py \
		tests/dirpoke.py | $(OBJDIR)
	@rm -rf $(DISKADB)
	@mkdir -p $(DISKADB)
	@for f in $(DISKA)/*; do b=`basename $$f`; \
		cmp -s $$f $(DISKADB)/$$b || cp $$f $(DISKADB)/$$b; done
	@for f in $(UDIRB); do b=`basename $$f`; \
		cmp -s $$f $(DISKADB)/$$b || cp $$f $(DISKADB)/$$b; done
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $@ $(CPMA_BLOCKS) $(DISKADB)
	python3 tests/dirpoke.py $@ $(DIRBNDBLK)
