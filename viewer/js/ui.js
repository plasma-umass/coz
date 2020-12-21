// Ensure the brower supports the File API
if (!window.File || !window.FileReader) {
    alert('The File APIs are not fully supported in this browser.');
}
var current_profile = undefined;
function get_min_points() {
    return d3.select('#minpoints_field').node().value;
}
// Returns the minimum number of path parts to include starting from the
// end of the path, in order for the resulting string to be unique.
function get_minimum_parts_for_unique_path(paths) {
    if (paths.length <= 1) {
        return 1;
    }
    var minimum = 1;
    var shortest_parts = Infinity;
    var is_unique = false;
    // Remove line numbers from paths
    paths = paths.map(function (path) { return path.replace(/:[0-9]*/, ''); });
    // Remove duplicate fully qualified path names
    paths = remove_duplicates(paths);
    // Special case: all of the paths are the same file. In that case, then we
    // only need to return the file name.
    var all_identical_paths = paths.every(function (path) { return path == paths[0]; });
    if (all_identical_paths) {
        return 1;
    }
    while (true) {
        var trimmed_paths = paths
            .map(function (path) {
            var parts = path.split('/');
            shortest_parts = Math.min(shortest_parts, parts.length);
            return parts.slice(parts.length - minimum, parts.length).join('/');
        });
        is_unique = !has_duplicates(trimmed_paths);
        if (is_unique) {
            return minimum;
        }
        else if (minimum >= shortest_parts) {
            // We can't possibly return a minimum parts needed that is greater than
            // the smallest parts possible
            return shortest_parts;
        }
        else {
            minimum += 1;
        }
    }
}
// Returns the last number of parts of a slash-separated path.
function get_last_path_parts(num_parts, path) {
    // Just return the path if it does not have any slash-separated parts
    if (path.indexOf('/') === -1) {
        return path;
    }
    var parts = path.split('/');
    return parts.slice(parts.length - num_parts, parts.length).join('/');
}
// This could be made simpler by using ES2015 Set instead.
function has_duplicates(array) {
    var seen = Object.create(null);
    for (var _i = 0, array_1 = array; _i < array_1.length; _i++) {
        var value = array_1[_i];
        if (value in seen) {
            return true;
        }
        seen[value] = true;
    }
    return false;
}
function remove_duplicates(array) {
    var uniq = {};
    for (var _i = 0, array_2 = array; _i < array_2.length; _i++) {
        var value = array_2[_i];
        uniq[value + '::' + typeof value] = value;
    }
    return Object.keys(uniq).map(function (key) { return uniq[key]; });
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
    d3.selectAll('.path').text(function (path) {
        // Filter out any paths which do not contain slash-separated parts
        if (path.indexOf('/') !== -1) {
            all_paths.push(path);
        }
        return path;
    });
    var minimum_parts = get_minimum_parts_for_unique_path(all_paths);
    // Shorten path strings
    var paths = d3.selectAll('.path')
        .classed('path', false).classed('shortpath', true)
        .text(function (path) { return get_last_path_parts(minimum_parts, path); })
        .attr('title', function (datum, index, outerIndex) {
        return datum;
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