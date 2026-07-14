# ebbackup CDC 内核叙事专册 — WSL XeLaTeX
MAIN = main
LATEX = xelatex
BUILDDIR = build

.PHONY: pdf clean

pdf:
	@mkdir -p $(BUILDDIR)
	$(LATEX) -output-directory=$(BUILDDIR) -interaction=nonstopmode $(MAIN).tex
	$(LATEX) -output-directory=$(BUILDDIR) -interaction=nonstopmode $(MAIN).tex
	@echo "==> PDF: $(BUILDDIR)/$(MAIN).pdf"

clean:
	rm -rf $(BUILDDIR) *.aux *.log *.out *.toc *.lof *.lot
