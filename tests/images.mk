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
	@for f in $(DISKA)/*; do b=`basename $$f`; \
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
