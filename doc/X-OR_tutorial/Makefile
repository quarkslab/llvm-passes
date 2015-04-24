# graphs
DOT=$(wildcard *.dot)
PNG=$(DOT:.dot=.png)

# rst
RST=$(wildcard *.rst)
HTML=$(RST:.rst=.html)

all: $(PNG) $(HTML)

$(PNG) : %.png : %.dot
	dot -Tpng $< -o $@

$(HTML) : %.html : %.rst
	rst2html $< > $@

clean: 
	/bin/rm -f *.html *.png
