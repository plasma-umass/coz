/**
 * Returns if this data point is valid.
 * Infinity / -Infinity occurs when dividing by 0.
 * NaN happens when the data is messed up and we get a NaN into a computation somewhere.
 */
function isValidDataPoint(data) {
    return !isNaN(data) && data !== Infinity && data !== -Infinity;
}
/**
 * Get the applicable data point from this particular experiment.
 * Returns -Infinity or Infinity in undefined cases.
 */
function getDataPoint(data) {
    // Discard obviously incorrect data points.
    if (data.duration < 0) {
        return NaN;
    }
    switch (data.type) {
        case 'throughput':
            return data.duration / data.delta;
        case 'latency':
            var arrivalRate = data.arrivals / data.duration;
            // Average latency, according to Little's Law.
            return data.difference / arrivalRate;
    }
}
/**
 * Are we trying to maximize or minimize this progress point?
 */
function shouldMaximize(data) {
    switch (data.type) {
        case 'latency':
            return false;
        case 'throughput':
            return true;
    }
}
function parseLine(s) {
    if (s[0] == '{') {
        return JSON.parse(s);
    }
    else {
        var parts = s.split('\t');
        var obj = { type: parts[0] };
        for (var i = 0; i < parts.length; i++) {
            var equals_index = parts[i].indexOf('=');
            if (equals_index === -1)
                continue;
            var key = parts[i].substring(0, equals_index);
            var value = parts[i].substring(equals_index + 1);
            switch (key) {
                case 'type':
                    if (obj.type === 'progress-point') {
                        key = 'point-type';
                    }
                    break;
                case 'delta':
                case 'time':
                case 'duration':
                case 'arrivals':
                case 'departures':
                case 'difference':
                    value = parseInt(value, 10);
                    break;
                case 'speedup':
                    value = parseFloat(value);
                    break;
            }
            obj[key] = value;
        }
        return obj;
    }
}
function max_normalized_area(d) {
    var max_normalized_area = 0;
    for (var i = 0; i < d.progress_points.length; i++) {
        var area = 0;
        for (var j = 1; j < d.progress_points[i].measurements.length; j++) {
            var prev_data = d.progress_points[i].measurements[j - 1];
            var current_data = d.progress_points[i].measurements[j];
            var avg_progress_speedup = (prev_data.progress_speedup + current_data.progress_speedup) / 2;
            area += avg_progress_speedup * (current_data.speedup - prev_data.speedup);
            var normalized_area = area / current_data.speedup;
            if (normalized_area > max_normalized_area)
                max_normalized_area = normalized_area;
        }
    }
    return max_normalized_area;
}
function max_progress_speedup(d) {
    var max_progress_speedup = 0;
    for (var i in d.progress_points) {
        for (var j in d.progress_points[i].measurements) {
            var progress_speedup = d.progress_points[i].measurements[j].progress_speedup;
            if (progress_speedup > max_progress_speedup)
                max_progress_speedup = progress_speedup;
        }
    }
    return max_progress_speedup;
}
function min_progress_speedup(d) {
    var min_progress_speedup = 0;
    for (var i in d.progress_points) {
        for (var j in d.progress_points[i].measurements) {
            var progress_speedup = d.progress_points[i].measurements[j].progress_speedup;
            if (progress_speedup < min_progress_speedup)
                min_progress_speedup = progress_speedup;
        }
    }
    return min_progress_speedup;
}
var sort_functions = {
    alphabetical: function (a, b) {
        if (a.name > b.name)
            return 1;
        else
            return -1;
    },
    impact: function (a, b) {
        if (max_normalized_area(b) > max_normalized_area(a))
            return 1;
        else
            return -1;
    },
    max_speedup: function (a, b) {
        if (max_progress_speedup(b) > max_progress_speedup(a))
            return 1;
        else
            return -1;
    },
    min_speedup: function (a, b) {
        if (min_progress_speedup(a) > min_progress_speedup(b))
            return 1;
        else
            return -1;
    }
};
var Profile = /** @class */ (function () {
    function Profile(profile_text, container, legend, get_min_points, display_warning) {
        this._data = {};
        this._disabled_progress_points = [];
        this._progress_points = null;
        this._plot_container = container;
        this._plot_legend = legend;
        this._get_min_points = get_min_points;
        this._display_warning = display_warning;
        var lines = profile_text.split('\n');
        var experiment = null;
        for (var i = 0; i < lines.length; i++) {
            if (lines[i].length == 0)
                continue;
            var entry = parseLine(lines[i]);
            if (entry.type === 'startup') {
                // Do nothing
            }
            else if (entry.type === 'shutdown') {
                // Do nothing
            }
            else if (entry.type === 'samples') {
                // Do nothing
            }
            else if (entry.type === 'runtime') {
                // Do nothing
            }
            else if (entry.type === 'experiment') {
                experiment = entry;
            }
            else if (entry.type === 'throughput-point' || entry.type === 'progress-point') {
                this.addThroughputMeasurement(experiment, entry);
            }
            else if (entry.type === 'latency-point') {
                this.addLatencyMeasurement(experiment, entry);
            }
            else {
                display_warning('Invalid Profile', 'The profile you loaded contains an invalid line: <pre>' + lines[i] + '</pre>');
            }
        }
        if (experiment == null) {
            display_warning('Empty Profile', 'The profile you loaded does not contain result from any performance experiments. Make sure you specified a progress point, built your program with debug information, and ran your program on an input that took at least a few seconds.');
        }
    }
    Profile.prototype.ensureDataEntry = function (selected, point, speedup, initial_value) {
        if (!this._data[selected])
            this._data[selected] = {};
        if (!this._data[selected][point])
            this._data[selected][point] = {};
        if (!this._data[selected][point][speedup])
            this._data[selected][point][speedup] = initial_value;
        return this._data[selected][point][speedup];
    };
    Profile.prototype.addThroughputMeasurement = function (experiment, point) {
        var entry = this.ensureDataEntry(experiment.selected, point.name, experiment.speedup, {
            delta: 0,
            duration: 0,
            type: 'throughput'
        });
        // Add new delta and duration to data
        entry.delta += point.delta;
        entry.duration += experiment.duration;
    };
    Profile.prototype.addLatencyMeasurement = function (experiment, point) {
        var entry = this.ensureDataEntry(experiment.selected, point.name, experiment.speedup, {
            arrivals: 0,
            departures: 0,
            difference: 0,
            duration: 0,
            type: 'latency'
        });
        entry.arrivals += point.arrivals;
        entry.departures += point.departures;
        // Compute a running weighted average of the difference between arrivals and departures
        if (entry.duration === 0) {
            entry.difference = point.difference;
        }
        else {
            // Compute the new total duration of all experiments combined (including the new one)
            var total_duration = entry.duration + experiment.duration;
            // Scale the difference down by the ratio of the prior and total durations. This scale factor will be closer to 1 than 0, so divide first for better numerical stability
            entry.difference *= entry.duration / total_duration;
            // Add the contribution to average difference from the current experiment. The scale factor will be close to zero, so multiply first for better numerical stability.
            entry.difference += (point.difference * experiment.duration) / total_duration;
        }
        // Update the total duration
        entry.duration += experiment.duration;
    };
    Profile.prototype.getProgressPoints = function () {
        if (this._progress_points) {
            return this._progress_points;
        }
        var points = [];
        for (var selected in this._data) {
            for (var point in this._data[selected]) {
                if (points.indexOf(point) === -1)
                    points.push(point);
            }
        }
        // Stable order.
        return this._progress_points = points.sort();
    };
    Profile.prototype.getSpeedupData = function (min_points) {
        var _this = this;
        var progress_points = this.getProgressPoints().filter(function (pp) { return _this._disabled_progress_points.indexOf(pp) === -1; });
        var result = [];
        for (var selected in this._data) {
            var points = [];
            var points_with_enough = 0;
            progress_point_loop: for (var i = 0; i < progress_points.length; i++) {
                // Set up an empty record for this progress point
                var point = {
                    name: progress_points[i],
                    measurements: new Array()
                };
                points.push(point);
                // Get the data for this progress point, if any
                var point_data = this._data[selected][progress_points[i]];
                // Check to be sure the point was observed and we have baseline (zero speedup) data
                if (point_data !== undefined && point_data[0] !== undefined) {
                    // Compute the baseline data point
                    var baseline_data_point = getDataPoint(point_data[0]);
                    var maximize = shouldMaximize(point_data[0]);
                    if (!isValidDataPoint(baseline_data_point)) {
                        // Baseline data point is invalid (divide by zero, or a NaN)
                        continue progress_point_loop;
                    }
                    // Loop over measurements and compute progress speedups in D3-friendly format
                    var measurements = [];
                    for (var speedup in point_data) {
                        var data_point = getDataPoint(point_data[speedup]);
                        // Skip invalid data.
                        if (!isValidDataPoint(data_point)) {
                            continue;
                        }
                        var progress_speedup = (baseline_data_point - data_point) / baseline_data_point;
                        if (!maximize) {
                            // We are trying to *minimize* this progress point, so negate the speedup.
                            progress_speedup = -progress_speedup;
                        }
                        // Skip really large negative and positive values
                        if (progress_speedup >= -1 && progress_speedup <= 2) {
                            // Add entry to measurements
                            measurements.push({
                                speedup: +speedup,
                                progress_speedup: progress_speedup
                            });
                        }
                    }
                    // Sort measurements by speedup
                    measurements.sort(function (a, b) { return a.speedup - b.speedup; });
                    // Use these measurements if we have enough different points
                    if (measurements.length >= min_points) {
                        points_with_enough++;
                        point.measurements = measurements;
                    }
                }
            }
            if (points_with_enough > 0) {
                result.push({
                    name: selected,
                    progress_points: points
                });
            }
        }
        return result;
    };
    Profile.prototype.drawLegend = function () {
        var _this = this;
        var container = this._plot_legend;
        var progress_points = this.getProgressPoints();
        var legend_entries_sel = container.selectAll('p.legend-entry').data(progress_points);
        legend_entries_sel.enter().append('p').attr('class', 'legend-entry');
        // Remove the noseries class from legend entries
        legend_entries_sel.classed('noseries', false).text('');
        legend_entries_sel.append('i')
            .attr('class', function (d, i) { return "fa fa-circle" + (_this._disabled_progress_points.indexOf(d) !== -1 ? '-o' : '') + " series" + i % 4; })
            .on('click', function (d, i) {
            var ind = _this._disabled_progress_points.indexOf(d);
            if (ind !== -1) {
                // Re-enable.
                _this._disabled_progress_points.splice(ind, 1);
            }
            else if (_this._disabled_progress_points.length + 1 < progress_points.length) {
                // Disable.
                _this._disabled_progress_points.push(d);
            }
            else {
                // This is the last enabled progress point. Forbid disabling it.
                _this._display_warning("Warning", "At least one progress point must be enabled.");
            }
            _this.drawPlots(true);
            _this.drawLegend();
            update();
        });
        legend_entries_sel.append('span')
            .attr('class', 'path')
            .text(function (d) { return d; });
        // Remove defunct legend entries
        legend_entries_sel.exit().remove();
    };
    Profile.prototype.drawPlots = function (no_animate) {
        var container = this._plot_container;
        var min_points = this._get_min_points();
        var speedup_data = this.getSpeedupData(min_points);
        var min_speedup = Infinity;
        var max_speedup = -Infinity;
        for (var i = 0; i < speedup_data.length; i++) {
            var result = speedup_data[i];
            var result_min = min_progress_speedup(result);
            var result_max = max_progress_speedup(result);
            if (result_min < min_speedup) {
                min_speedup = result_min;
            }
            if (result_max > max_speedup) {
                max_speedup = result_max;
            }
        }
        // Give some wiggle room to display points.
        min_speedup *= 1.05;
        max_speedup *= 1.05;
        /****** Compute dimensions ******/
        var container_width = parseInt(container.style('width'));
        // Add columns while maintaining a target width
        var cols = 1;
        while (container_width / (cols + 1) >= 300)
            cols++;
        var div_width = container_width / cols;
        var div_height = 190;
        var svg_width = div_width - 10;
        var svg_height = div_height - 40;
        var margins = { left: 60, right: 20, top: 10, bottom: 35 };
        var plot_width = svg_width - margins.left - margins.right;
        var plot_height = svg_height - margins.top - margins.bottom;
        var radius = 3;
        var tick_size = 6;
        // Formatters
        var axisFormat = d3.format('.0%');
        var percentFormat = d3.format('+.1%');
        // Scales
        var xscale = d3.scale.linear().domain([0, 1]);
        var yscale = d3.scale.linear().domain([min_speedup, max_speedup]);
        // Axes
        var xaxis = d3.svg.axis()
            .scale(xscale)
            .orient('bottom')
            .ticks(5)
            .tickSize(tick_size)
            .tickFormat(axisFormat);
        //.tickSize(tick_size, 0, 0)
        //.tickFormat(axisFormat);
        var yaxis = d3.svg.axis()
            .scale(yscale)
            .orient('left')
            .ticks(5)
            .tickSize(tick_size, 0, 0)
            .tickFormat(axisFormat);
        // Gridlines
        var xgrid = d3.svg.axis().scale(xscale).orient('bottom').tickFormat('');
        var ygrid = d3.svg.axis().scale(yscale).orient('left').tickFormat('').ticks(5);
        // Tooltip
        var tip = d3.tip()
            .attr('class', 'd3-tip')
            .offset([-5, 0])
            .html(function (d) {
            return '<strong>Line Speedup:</strong> ' + percentFormat(d.speedup) + '<br>' +
                '<strong>Progress Speedup:</strong> ' + percentFormat(d.progress_speedup);
        })
            .direction(function (d) {
            if (d.speedup > 0.8)
                return 'w';
            else
                return 'n';
        });
        /****** Add or update divs to hold each plot ******/
        var plot_div_sel = container.selectAll('div.plot')
            .data(speedup_data, function (d) { return d.name; });
        var plot_x_pos = function (d, i) {
            var col = i % cols;
            return (col * div_width) + 'px';
        };
        var plot_y_pos = function (d, i) {
            var row = (i - (i % cols)) / cols;
            return (row * div_height) + 'px';
        };
        // First, remove divs that are disappearing
        plot_div_sel.exit().transition().duration(200)
            .style('opacity', 0).remove();
        // Insert new divs with zero opacity
        plot_div_sel.enter().append('div')
            .attr('class', 'plot')
            .style('margin-bottom', -div_height + 'px')
            .style('opacity', 0)
            .style('width', div_width);
        // Sort plots by the chosen sorting function
        plot_div_sel.sort(sort_functions[d3.select('#sortby_field').node().value]);
        // Move divs into place. Only animate if we are not on a resizing redraw
        if (!no_animate) {
            plot_div_sel.transition().duration(400).delay(200)
                .style('top', plot_y_pos)
                .style('left', plot_x_pos)
                .style('opacity', 1);
        }
        else {
            plot_div_sel.style('left', plot_x_pos)
                .style('top', plot_y_pos);
        }
        /****** Insert, remove, and update plot titles ******/
        var plot_title_sel = plot_div_sel.selectAll('div.plot-title').data(function (d) { return [d.name]; });
        plot_title_sel.enter().append('div').attr('class', 'plot-title');
        plot_title_sel.text(function (d) { return d; })
            .classed('path', true)
            .attr('title', function (d) { return d; })
            .style('width', div_width + 'px');
        plot_title_sel.exit().remove();
        /****** Update scales ******/
        xscale.domain([0, 1]).range([0, plot_width]);
        yscale.domain([min_speedup, max_speedup]).range([plot_height, 0]);
        /****** Update gridlines ******/
        xgrid.tickSize(-plot_height, 0, 0);
        ygrid.tickSize(-plot_width, 0, 0);
        /****** Insert and update plot svgs ******/
        var plot_svg_sel = plot_div_sel.selectAll('svg').data([1]);
        plot_svg_sel.enter().append('svg');
        plot_svg_sel.attr('width', svg_width)
            .attr('height', svg_height)
            .call(tip);
        plot_svg_sel.exit().remove();
        /****** Add or update plot areas ******/
        var plot_area_sel = plot_svg_sel.selectAll('g.plot_area').data([0]);
        plot_area_sel.enter().append('g').attr('class', 'plot_area');
        plot_area_sel.attr('transform', "translate(" + margins.left + ", " + margins.top + ")");
        plot_area_sel.exit().remove();
        /****** Add or update clip paths ******/
        var clippath_sel = plot_area_sel.selectAll('#clip').data([0]);
        clippath_sel.enter().append('clipPath').attr('id', 'clip');
        clippath_sel.exit().remove();
        /****** Add or update clipping rectangles to clip paths ******/
        var clip_rect_sel = clippath_sel.selectAll('rect').data([0]);
        clip_rect_sel.enter().append('rect');
        clip_rect_sel.attr('x', -radius - 1)
            .attr('y', 0)
            .attr('width', plot_width + 2 * radius + 2)
            .attr('height', plot_height);
        clip_rect_sel.exit().remove();
        /****** Select plots areas, but preserve the real speedup data ******/
        plot_area_sel = plot_div_sel.select('svg').select('g.plot_area');
        /****** Add or update x-grids ******/
        var xgrid_sel = plot_area_sel.selectAll('g.xgrid').data([0]);
        xgrid_sel.enter().append('g').attr('class', 'xgrid');
        xgrid_sel.attr('transform', "translate(0, " + plot_height + ")").call(xgrid);
        xgrid_sel.exit().remove();
        /****** Add or update y-grids ******/
        var ygrid_sel = plot_area_sel.selectAll('g.ygrid').data([0]);
        ygrid_sel.enter().append('g').attr('class', 'ygrid');
        ygrid_sel.call(ygrid);
        ygrid_sel.exit().remove();
        /****** Add or update x-axes ******/
        var xaxis_sel = plot_area_sel.selectAll('g.xaxis').data([0]);
        xaxis_sel.enter().append('g').attr('class', 'xaxis');
        xaxis_sel.attr('transform', "translate(0, " + plot_height + ")").call(xaxis);
        xaxis_sel.exit().remove();
        /****** Add or update x-axis titles ******/
        var xtitle_sel = plot_area_sel.selectAll('text.xtitle').data([0]);
        xtitle_sel.enter().append('text').attr('class', 'xtitle');
        xtitle_sel.attr('x', xscale(0.5))
            .attr('y', 32) // Approximate height of the x-axis
            .attr('transform', "translate(0, " + plot_height + ")")
            .style('text-anchor', 'middle')
            .text('Line speedup');
        xtitle_sel.exit().remove();
        /****** Add or update y-axes ******/
        var yaxis_sel = plot_area_sel.selectAll('g.yaxis').data([0]);
        yaxis_sel.enter().append('g').attr('class', 'yaxis');
        yaxis_sel.call(yaxis);
        yaxis_sel.exit().remove();
        /****** Add or update y-axis title ******/
        var ytitle_sel = plot_area_sel.selectAll('text.ytitle').data([0]);
        ytitle_sel.enter().append('text').attr('class', 'ytitle');
        ytitle_sel.attr('x', -(svg_height - margins.bottom) / 2)
            .attr('y', -45) // Approximate width of y-axis
            .attr('transform', 'rotate(-90)')
            .style('text-anchor', 'middle')
            .style('alignment-baseline', 'central')
            .text('Program Speedup');
        ytitle_sel.exit().remove();
        /****** Add or update x-zero line ******/
        var xzero_sel = plot_area_sel.selectAll('line.xzero').data([0]);
        xzero_sel.enter().append('line').attr('class', 'xzero');
        xzero_sel.attr('x1', xscale(0))
            .attr('y1', 0)
            .attr('x2', xscale(0))
            .attr('y2', plot_height + tick_size);
        xzero_sel.exit().remove();
        /****** Add or update y-zero line ******/
        var yzero_sel = plot_area_sel.selectAll('line.yzero').data([0]);
        yzero_sel.enter().append('line').attr('class', 'yzero');
        yzero_sel.attr('x1', -tick_size)
            .attr('y1', yscale(0))
            .attr('x2', plot_width)
            .attr('y2', yscale(0));
        yzero_sel.exit().remove();
        /****** Add or update series ******/
        var progress_points = this.getProgressPoints();
        var series_sel = plot_area_sel.selectAll('g.series')
            .data(function (d) { return d.progress_points; }, function (d) { return d.name; });
        series_sel.enter().append('g');
        series_sel.attr('class', function (d, k) {
            // Use progress point's position in array to assign it a stable color, no matter
            // which points are enabled for display.
            return "series series" + (progress_points.indexOf(d.name)) % 5;
        })
            .attr('style', 'clip-path: url(#clip);');
        series_sel.exit().remove();
        /****** Add or update trendlines ******/
        // Configure a loess smoother
        var loess = science.stats.loess()
            .bandwidth(0.4)
            .robustnessIterations(5);
        // Create an svg line to draw the loess curve
        var line = d3.svg.line().x(function (d) { return xscale(d[0]); })
            .y(function (d) { return yscale(d[1]); })
            .interpolate('basis');
        // Apply the loess smoothing to each series, then draw the lines
        var lines_sel = series_sel.selectAll('path').data(function (d) {
            var xvals = d.measurements.map(function (e) { return e.speedup; });
            var yvals = d.measurements.map(function (e) { return e.progress_speedup; });
            var smoothed_y = [];
            try {
                smoothed_y = loess(xvals, yvals);
            }
            catch (e) {
                // Bandwidth too small error. Ignore and proceed with empty smoothed line.
            }
            // Speedup is always zero for a line speedup of zero
            smoothed_y[0] = 0;
            if (xvals.length > 5)
                return [d3.zip(xvals, smoothed_y)];
            else
                return [d3.zip(xvals, yvals)];
        });
        lines_sel.enter().append('path');
        lines_sel.attr('d', line);
        lines_sel.exit().remove();
        /****** Add or update points ******/
        var points_sel = series_sel.selectAll('circle').data(function (d) { return d.measurements; });
        points_sel.enter().append('circle').attr('r', radius);
        points_sel.attr('cx', function (d) { return xscale(d.speedup); })
            .attr('cy', function (d) { return yscale(d.progress_speedup); })
            .on('mouseover', function (d, i) {
            d3.select(this).classed('highlight', true);
            tip.show(d, i);
        })
            .on('mouseout', function (d, i) {
            d3.select(this).classed('highlight', false);
            tip.hide(d, i);
        });
        points_sel.exit().remove();
        // Return the number of plots
        return speedup_data.length;
    };
    return Profile;
}());
//# sourceMappingURL=profile.js.map