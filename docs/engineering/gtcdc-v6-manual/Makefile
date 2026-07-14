# G-TCDC v6 2F-Gear 工程技术手册 — XeLaTeX 构建
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
	rm -rf $(BUILDDIR) *.aux *.log *.out *.toc *.lof *.lot *.lol
