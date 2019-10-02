// Ensure the brower supports the File API
if (!window.File || !window.FileReader) {
    alert('The File APIs are not fully supported in this browser.');
}
var current_profile = undefined;
function get_min_points() {
    return d3.select('#minpoints_field').node().value;
}
// Returns the longest common path prefix between all strings in an array
// Ex: get_common_path_prefix(["/a/b/c/", "/a/b/d", "/a/e/d/"]) => "/a/"
// Ex: get_common_path_prefix(["/a/b/test.c:44", "/a/b/util.c:123"]) => "/a/b/"
function get_common_path_prefix(paths) {
    if (paths.length === 1) {
        return "";
    }
    var A = paths.concat().sort();
    var shortest = paths.reduce(function (min, str) { return min < str ? min : str; }, paths[0]);
    var longest = paths.reduce(function (min, str) { return min > str ? min : str; }, paths[0]);
    var last_slash_index = shortest.lastIndexOf("/");
    var i = 0;
    var end = shortest.length;
    // If there is a last slash, then we will stop there, so that we do not
    // cut off the name of the file.
    if (last_slash_index !== -1) {
        end = last_slash_index + 1;
    }
    while (i < end && shortest.charAt(i) === longest.charAt(i)) {
        i++;
    }
    var prefix = shortest.substring(0, i);
    // Check if the string slicing produced a path with a cutoff word.
    // Ex: "/a/b/c/some_path" => "/a/b/c/some"
    // We want to turn that into "/a/b/c/" in that case.
    var last_slash = prefix.lastIndexOf("/");
    console.log(prefix);
    console.log(last_slash);
    console.log(prefix.substring(0, last_slash));
    if (last_slash !== -1 && last_slash < prefix.length - 1) {
        prefix = prefix.substring(0, last_slash + 1);
    }
    return prefix;
}
// Returns a set of the common path prefixes among a set of strings. The number
// of prefixes is determined by how many unique path prefixes there are within
// the first slash.
// Ex: get_common_path_prefixes([
//    "/test/a/b/c/d/file.c:123",
//    "/test/a/b/c/f/file.c:123",
//    "/other/a/b/c/d/file.c:123",
//    "/other/a/c/file.c:321",
//    "/a/b/c/file.c:100",
// ]) => ["/test/a/b/c/", "/other/a/", "/a/b/c/"]
function get_common_path_prefixes(paths) {
    // Paths grouped by the first path part
    var grouped_paths = {};
    for (var _i = 0, paths_1 = paths; _i < paths_1.length; _i++) {
        var path = paths_1[_i];
        var first_slash = path.indexOf("/");
        var second_slash = path.indexOf("/", first_slash + 1);
        var has_leading_slash = first_slash === 0;
        var has_second_slash = second_slash !== -1;
        // Includes paths that have two slashes like:
        // * /a/b/c/file.c:123
        // * /a/file.c:123
        // * a/b/file.c:123
        // Does not include paths like:
        // * a/file.c:123
        // * file.c:123
        // * /file.c:123
        if (has_second_slash) {
            var parts = path.split('/');
            // Includes paths that have a leading slash like:
            // * /a/b/c/file.c:123
            // * /a/file.c:123
            // Does not include paths like:
            // * a/b/file.c:123
            var initial_prefix = parts[0];
            if (has_leading_slash) {
                // If path is "/a/b/c/file.c:123", then prefix is "/a"
                initial_prefix = "/" + parts[1];
            }
            if (grouped_paths[initial_prefix] === undefined) {
                grouped_paths[initial_prefix] = [path];
            }
            else {
                grouped_paths[initial_prefix].push(path);
            }
        }
    }
    var common_prefixes = [];
    for (var prefix in grouped_paths) {
        var paths_2 = grouped_paths[prefix];
        console.log(paths_2);
        common_prefixes.push(get_common_path_prefix(paths_2));
    }
    return common_prefixes;
}
{
    var common_path_prefixes = get_common_path_prefixes([
        "file.c:123",
        "file.c:123",
        "a/file.c:123",
        "a/file.c:123",
        "/test/a/b/c/d/file.c:123",
        "/test/a/b/c/f/file.c:123",
        "/other/a/b/c/d/file.c:123",
        "/other/a/c/file.c:321",
        "/a/b/c/file.c:100",
        "/a/b/d/file.c:123",
    ]);
    console.log("expected:", ["/test/a/b/c/", "/other/a/", "/a/b/"]);
    console.log("got:", common_path_prefixes);
}
// Given the common path prefix (from get_common_prefix), this will return the
// unique part of the path which should be displayable. This also checks
// for leading slashes after the slicing, which will be removed.
// Ex: get_unique_path_part("/a/b/c/", "/a/b/c/test.cpp") => "test.cpp"
// Ex: get_unique_path_aprt("/a/b/c/", "util.c") => "util.c"
function get_unique_path_part(common_paths, path) {
    // For the same reasons as the common path collection logic, we will
    // not shorten any paths which do not contain a slash, which is hopefully
    // just paths that are only a file name.
    for (var _i = 0, common_paths_1 = common_paths; _i < common_paths_1.length; _i++) {
        var common_path = common_paths_1[_i];
        // Check if the prefix applies to this path
        if (path.indexOf(common_path) !== -1) {
            return path.substr(common_path.length, path.length);
        }
    }
    return path;
}
{
    var test_data = [
        "/home/fitzgen/walrus/src/lib.rs",
        "/home/fitzgen/walrus/src/passes/mod.rs",
        "/home/fitzgen/rayon/src/lib.rs",
        "/home/fitzgen/rayon/src/spawner/mod.rs"
    ];
    var common_paths = get_common_path_prefixes(test_data);
    console.log(common_paths);
    console.log(common_paths[0] === "/home/fitzgen/");
    console.log(get_unique_path_part(common_paths, "/home/fitzgen/walrus/src/lib.rs") === "walrus/src/lib.rs");
    console.log(get_unique_path_part(common_paths, "/home/fitzgen/rayon/src/lib.rs") === "rayon/src/lib.rs");
}
{
    var test_data = [
        "/a/b/c/test.cpp:135",
        "/a/b/c/test.cpp:145",
        "/a/b/c/test.cpp:251",
    ];
    var common_paths = get_common_path_prefixes(test_data);
    console.log(common_paths);
    console.log(common_paths[0] === "/a/b/c/");
    console.log(get_unique_path_part(common_paths, "/a/b/c/test.cpp:135") === "test.cpp:135");
    console.log(get_unique_path_part(common_paths, "/a/b/c/d/util.c:12") === "d/util.c:12");
    console.log(get_unique_path_part(common_paths, "/a/b/c/test.cpp:23094345") === "test.cpp:23094345");
}
{
    var test_data = [
        "src/lib.rs:330",
        "src/lib.rs:367",
        "/rustc/eae3437dfe991621e8afdc82734f4a172d7ddf9b/src/libcore/slice/mod.rs:3261",
        "/rustc/eae3437dfe991621e8afdc82734f4a172d7ddf9b/src/libcore/slice/mod.rs:5348",
        "/rustc/eae3437dfe991621e8afdc82734f4a172d7ddf9b/src/libcore/ptr/mod.rs:197",
        "/rustc/eae3437dfe991621e8afdc82734f4a172d7ddf9b/src/liballoc/vec.rs:1563",
        "/rustc/eae3437dfe991621e8afdc82734f4a172d7ddf9b/src/liballoc/vec.rs:1787",
        "/home/cmchenry/.cargo/registry/src/github.com-1ecc6299db9ec823/memchr-2.2.1/src/x86/sse2.rs:0",
        "/home/cmchenry/.cargo/registry/src/github.com-1ecc6299db9ec823/parse_wiki_text-0.1.5/src/table.rs:288",
        "/home/cmchenry/.cargo/registry/src/github.com-1ecc6299db9ec823/quick-xml-0.16.1/src/reader.rs:0",
    ];
    var common_paths = get_common_path_prefixes(test_data);
    console.log(common_paths);
    console.log(common_paths.indexOf("/rustc/eae3437dfe991621e8afdc82734f4a172d7ddf9b/src/") !== -1);
    console.log(common_paths.indexOf("/home/cmchenry/.cargo/registry/src/github.com-1ecc6299db9ec823/") !== -1);
    console.log(common_paths.indexOf("src/") === -1);
    console.log(get_unique_path_part(common_paths, "/rustc/eae3437dfe991621e8afdc82734f4a172d7ddf9b/src/libcore/slice/mod.rs:3261") === "libcore/slice/mod.rs:3261");
    console.log(get_unique_path_part(common_paths, "/home/cmchenry/.cargo/registry/src/github.com-1ecc6299db9ec823/memchr-2.2.1/src/x86/sse2.rs:0") === "memchr-2.2.1/src/x86/sse2.rs:0");
}
function display_warning(title, text) {
    var warning = $("<div class=\"alert alert-warning alert-dismissible\" role=\"alert\">\n      <button type=\"button\" class=\"close\" data-dismiss=\"alert\" aria-label=\"Close\"><span aria-hidden=\"true\">&times;</span></button>\n      <strong>" + title + ":</strong> " + text + "\n    </div>");
    $('#warning-area').append(warning);
    // Fade out after 5 seconds.
    setTimeout(function () {
        warning.fadeOut(500, function () {
            warning.alert('close');
        });
    }, 5000);
}
function update(resize) {
    if (current_profile === undefined)
        return;
    // Enable the sortby field
    d3.select('#sortby_field').attr('disabled', null);
    // Draw plots
    var num_plots = current_profile.drawPlots(resize);
    // Clear the message area
    d3.select('#plot-message').text('');
    // Display a warning if there are no plots
    if (num_plots == 0) {
        d3.select('#plot-message').html('<h1>No Data to Plot</h1><p>Your profile does not contain enough observations to generate any plots. Try reducing the minimum number of points required to show a plot using the slider, or run your program for a longer time to collect more data.</p>');
    }
    // Draw the legend
    current_profile.drawLegend();
    var tooltip = d3.select("body")
        .append("div")
        .style("position", "absolute")
        .style("z-index", "10")
        .style("visibility", "hidden");
    var all_paths = [];
    // Collect all of the paths that we have, in order to calculate what the
    // common path prefix is among all of the paths.
    d3.selectAll('.path')
        .text(function (path) {
        // Do not consider any paths which do not contain a slash as a way of
        // filtering out paths which are already just the file name.
        if (path.indexOf("/") !== -1) {
            all_paths.push(path);
        }
        return path;
    });
    var common_path_prefixes = get_common_path_prefixes(all_paths);
    console.log(all_paths);
    console.log("prefixes: ", common_path_prefixes);
    // Shorten path strings
    var paths = d3.selectAll('.path')
        .classed('path', false).classed('shortpath', true)
        .text(function (path) {
        return get_unique_path_part(common_path_prefixes, path);
    });
}
// Set a handler for the load profile button
d3.select('#load-profile-btn').on('click', function () {
    // Reset the filename field
    d3.select('#load-profile-filename').attr('value', '');
    // Disable the open button
    d3.select('#load-profile-open-btn').classed('disabled', true);
});
// Set a handler for the fake browse button
d3.select('#load-profile-browse-btn').on('click', function () {
    $('#load-profile-file').trigger('click');
});
// Set a handler for file selection
d3.select('#load-profile-file').on('change', function () {
    var file_browser = this;
    var open_button = d3.select('#load-profile-open-btn');
    d3.select('#load-profile-filename').attr('value', file_browser.value.replace(/C:\\fakepath\\/i, ''));
    open_button.classed('disabled', false)
        .on('click', function () {
        var reader = new FileReader();
        reader.onload = function (event) {
            var contents = event.target.result;
            current_profile = new Profile(contents, d3.select('#plot-area'), d3.select('#legend'), get_min_points, display_warning);
            update();
        };
        reader.onerror = function (event) {
            console.error("Unable to read file. Error code: " + event.target.error.code);
        };
        // Read the profile
        reader.readAsText(file_browser.files[0]);
        // Clear the file browser value
        file_browser.value = '';
    });
});
// Update the plots and minpoints display when dragged or clicked
d3.select('#minpoints_field').on('input', function () {
    d3.select('#minpoints_display').text(this.value);
    update();
});
d3.select('#sortby_field').on('change', update);
d3.select(window).on('resize', function () { update(true); });
var sample_profiles = ['blackscholes', 'dedup', 'ferret', 'fluidanimate', 'sqlite', 'swaptions'];
var sample_profile_objects = {};
var samples_sel = d3.select('#samples').selectAll('.sample-profile').data(sample_profiles)
    .enter().append('button')
    .attr('class', 'btn btn-sm btn-default sample-profile')
    .attr('data-dismiss', 'modal')
    .attr('loaded', 'no')
    .text(function (d) { return d; })
    .on('click', function (d) {
    var sel = d3.select(this);
    if (sel.attr('loaded') !== 'yes') {
        // Avoid race condition: Set first.
        sel.attr('loaded', 'yes');
        var xhr_1 = new XMLHttpRequest();
        xhr_1.open('GET', "profiles/" + d + ".coz");
        xhr_1.onload = function () {
            current_profile = sample_profile_objects[d] =
                new Profile(xhr_1.responseText, d3.select('#plot-area'), d3.select('#legend'), get_min_points, display_warning);
            update();
        };
        xhr_1.onerror = function () {
            sel.attr('loaded', 'no');
            display_warning("Error", "Failed to load profile for " + d + ".");
        };
        xhr_1.send();
    }
    else {
        current_profile = sample_profile_objects[d];
        update();
    }
});
//# sourceMappingURL=ui.js.map