.PHONY: all slides script clean
BUILD = build
XELATEX = xelatex -output-directory=$(BUILD) -interaction=nonstopmode -halt-on-error

all: slides script

$(BUILD):
	@mkdir -p $(BUILD)

slides: $(BUILD)
	$(XELATEX) -jobname=slides main-beamer.tex
	$(XELATEX) -jobname=slides main-beamer.tex
	@echo "==> $(BUILD)/slides.pdf"

script: $(BUILD)
	$(XELATEX) -jobname=script main-script.tex
	$(XELATEX) -jobname=script main-script.tex
	@echo "==> $(BUILD)/script.pdf"

clean:
	rm -rf $(BUILD) *.aux *.log *.nav *.out *.snm *.toc *.vrb
