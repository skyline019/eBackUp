# ebbackup Windows 原生元数据专册 — 每板块独立 PDF（XeLaTeX）
LATEX = xelatex -interaction=nonstopmode
BUILDDIR = build

DOCS = \
  01-manifest-v5 \
  02-scan-enrichment \
  03-acl-sd \
  04-ads \
  05-hardlink-inode \
  06-reparse-junction \
  07-sparse-ntfs \
  08-efs \
  09-vss

.PHONY: all clean list

all: $(DOCS)

$(DOCS): %: $(BUILDDIR)/%.pdf

$(BUILDDIR)/%.pdf: main/%.tex preamble.tex chapters/%.tex
	@mkdir -p $(BUILDDIR)
	$(LATEX) -output-directory=$(BUILDDIR) -jobname=$* main/$*.tex
	$(LATEX) -output-directory=$(BUILDDIR) -jobname=$* main/$*.tex
	@echo "==> $(BUILDDIR)/$*.pdf"

clean:
	rm -rf $(BUILDDIR)

list:
	@echo "Targets: $(DOCS)"
