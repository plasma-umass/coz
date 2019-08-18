all: $(wildcard js/*.js)

js/%.js: ts/%.ts tsconfig.json
	npm install

clean:
	rm -f js/*
