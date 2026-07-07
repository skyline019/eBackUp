.PHONY: pdf clean
pdf:
	@mkdir -p build
	xelatex -output-directory=build -interaction=nonstopmode main.tex
	xelatex -output-directory=build -interaction=nonstopmode main.tex
	@echo "==> PDF: build/main.pdf"

clean:
	rm -rf build
