// From: https://github.com/mmig/libflac.js/blob/master/tools/extract_EXPORTED_FUNCTIONS.js
/**
 * HELPER script for extracting exported C functions
 * (to be used for emscripten compiler setting EXPORTED_FUNCTIONS)
 */

var fs = require('fs'),
	path = require('path');

var str = process.argv.slice(2).map(x => {
	return fs.readFileSync(path.join(__dirname, x), 'utf8')
}).join("\n")

var re = /.c((wrap)|(call))\(\s*?['"](.*?)['"]/igm
var direct = /\._(.*?)\(/igm

var res;
var funcs = new Set();
while(res = re.exec(str)){
	funcs.add(res[4]);
}
while(res = direct.exec(str)){
	funcs.add(res[1]);
}
funcs.add("malloc")
funcs.add("free")
var list = Array.from(funcs).sort(function(a, b){
	return a.localeCompare(b);
});
console.error("Exported functions:")
console.error(list.join("\n"))

var len = list.length;
var sb = list.map(function(f, i){
	return '"_' + f + '"' + (i === len -1? '' : ',');
});


sb.unshift("-s EXPORTED_FUNCTIONS=[");
sb.push("]");

console.log(sb.join(''));
