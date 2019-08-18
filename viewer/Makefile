all: js/ui.js js/profile.js

js/%.js: ts/%.ts tsconfig.json
	npm install

clean:
	rm -f js/*
