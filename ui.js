// Ensure the brower supports the File API
if (!window.File || !window.FileReader) {
    alert('The File APIs are not fully supported in this browser.');
}
var current_profile = undefined;
function get_min_points() {
    return d3.select('#minpoints_field').node().value;
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
    current_profile.drawPlots(resize);
    // Draw the legend
    current_profile.drawLegend();
    var tooltip = d3.select("body")
        .append("div")
        .style("position", "absolute")
        .style("z-index", "10")
        .style("visibility", "hidden");
    // Shorten path strings
    var paths = d3.selectAll('.path')
        .classed('path', false).classed('shortpath', true)
        .text(function (d) {
        var parts = d.split('/');
        var filename = parts[parts.length - 1];
        return filename;
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
var samples_sel = d3.select('#samples').selectAll('.sample-profile').data(sample_profiles)
    .enter().append('button')
    .attr('class', 'btn btn-sm btn-default sample-profile')
    .attr('data-dismiss', 'modal')
    .attr('loaded', 'no')
    .text(function (d) { return d; })
    .on('click', function (d) {
    var sel = d3.select(this);
    if (sel.attr('loaded') != 'yes') {
        d3.select('body').append('script')
            .attr('src', 'profiles/' + d + '.coz.js')
            .on('load', function () {
            sel.attr('loaded', 'yes');
            current_profile = eval(d + '_profile');
            update();
        });
    }
    else {
        current_profile = eval(d + '_profile');
        update();
    }
});
//# sourceMappingURL=ui.js.map