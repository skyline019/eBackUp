# ebbackup 备份内核工程技术文档 — XeLaTeX 构建
MAIN = main
LATEX = xelatex
BUILDDIR = build

.PHONY: pdf clean watch

pdf:
	@mkdir -p $(BUILDDIR)
	$(LATEX) -output-directory=$(BUILDDIR) -interaction=nonstopmode $(MAIN).tex
	$(LATEX) -output-directory=$(BUILDDIR) -interaction=nonstopmode $(MAIN).tex
	@echo "==> PDF: $(BUILDDIR)/$(MAIN).pdf"

clean:
	rm -rf $(BUILDDIR) *.aux *.log *.out *.toc *.lof *.lot *.lol

watch:
	@echo "Run: xelatex -output-directory=build main.tex (twice after edits)"
