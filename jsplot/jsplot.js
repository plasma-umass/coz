// Ensure the brower supports the File API
if (!window.File || !window.FileReader) {
  alert('The File APIs are not fully supported in this browser.');
}

// Create the minpoints slider
var minpoints_slider = new Slider('#minpoints_field', {
  min: 0,
  max: 20,
  step: 1,
  value: 10,
  ticks: [0, 5, 10, 15, 20],
  ticks_labels: [0, 5, 10, 15, 20]
});

function Profile(container) {
  // map from sped up location -> progress point -> speedup amount -> (progress delta, duration)
  this.data = {};
  
  this.addProgressMeasurement = function(selected, point, speedup, delta, duration) {
    // Add entry for selected line if needed
    if(!this.data[selected]) this.data[selected] = {};
    
    // Add entry for progress point if needed
    if(!this.data[selected][point]) this.data[selected][point] = {};
    
    // Add entry for progress point if needed
    if(!this.data[selected][point][speedup]) {
      this.data[selected][point][speedup] = {
        delta: 0,
        duration: 0
      };
    }
    
    // Add new delta and duration to data
    this.data[selected][point][speedup].delta += delta;
    this.data[selected][point][speedup].duration += duration;
  };
  
  this.getProgressPoints = function() {
    var points = [];
    for(var selected in this.data) {
      for(var point in this.data[selected]) {
        if(points.indexOf(point) == -1) points.push(point);
      }
    }
    return points;
  };
  
  this.getSpeedupData = function(min_points) {
    var progress_points = this.getProgressPoints();
    var result = [];
    for(var selected in this.data) {
      var points = [];
      var points_with_enough = 0;
      for(var i=0; i<progress_points.length; i++) {
        // Set up an empty record for this progress point
        var point = {
          name: progress_points[i],
          measurements: []
        };
        
        // Get the data for this progress point, if any
        var point_data = this.data[selected][progress_points[i]];
        
        // Check to be sure the point was observed and we have baseline (zero speedup) data
        if(point_data !== undefined && point_data[0] !== undefined && point_data[0].delta > 0) {
          // Compute the baseline progress period
          var baseline_period = point_data[0].duration / point_data[0].delta;
          
          // Loop over measurements and compute progress speedups in D3-friendly format
          var measurements = [];
          for(var speedup in point_data) {
            // Skip this speedup measurement if period is undefined
            if(point_data[speedup].delta == 0) continue;
          
            // Compute progress period for this speedup size
            var period = point_data[speedup].duration / point_data[speedup].delta;
          
            // Add entry to measurements
            measurements.push({
              speedup: +speedup,
              progress_speedup: (baseline_period - period) / baseline_period
            });
          }
          
          // Sort measurements by speedup
          measurements.sort(function(a, b) { return a.speedup - b.speedup; });
          
          // Use these measurements if we have enough different points
          if(measurements.length >= min_points) {
            points_with_enough++;
            point.measurements = measurements;
          }
        }
        
        points.push(point);
      }
      
      if(points_with_enough > 0) {
        result.push({
          name: selected,
          progress_points: points
        });
      }
    }
    
    return result;
  }
  
  // Formatters
  var axisFormat = d3.format('.0%');
  var percentFormat = d3.format('+.1%');
  
  // Scales
  var xscale = d3.scale.linear().domain([0, 1]);
  var yscale = d3.scale.linear().domain([-1, 1]);
  
  // Axes
  var xaxis = d3.svg.axis().scale(xscale).orient('bottom').ticks(5).tickFormat(axisFormat);
  var yaxis = d3.svg.axis().scale(yscale).orient('left').ticks(5).tickFormat(axisFormat);
  
  // Gridlines
  var xgrid = d3.svg.axis().scale(xscale).orient('bottom').tickFormat('');
  var ygrid = d3.svg.axis().scale(yscale).orient('left').tickFormat('').ticks(5);
  
  // Tooltip
  var tip = d3.tip()
    .attr('class', 'd3-tip')
    .offset([-5, 0])
    .html(function(d) {
      return '<strong>Line Speedup:</strong> ' + percentFormat(d.speedup) + '<br>' +
             '<strong>Progress Speedup:</strong> ' + percentFormat(d.progress_speedup);
    })
    .direction(function(d) {
      if(d.speedup > 0.8) return 'w';
      else return 'n';
    });
  
  function max_normalized_area(d) {
    var max_normalized_area = 0;
    for(var i=0; i<d.progress_points.length; i++) {
      var area = 0;
      for(var j=1; j<d.progress_points[i].measurements.length; j++) {
        var prev_data = d.progress_points[i].measurements[j-1];
        var current_data = d.progress_points[i].measurements[j];
        var avg_progress_speedup = (prev_data.progress_speedup + current_data.progress_speedup) / 2;
        area += avg_progress_speedup * (current_data.speedup - prev_data.speedup);
        var normalized_area = area / current_data.speedup;
        if(normalized_area > max_normalized_area) max_normalized_area = normalized_area;
      }
    }
    return max_normalized_area;
  }
  
  function max_progress_speedup(d) {
    var max_progress_speedup = 0;
    for(var i in d.progress_points) {
      for(var j in d.progress_points[i].measurements) {
        var progress_speedup = d.progress_points[i].measurements[j].progress_speedup;
        if(progress_speedup > max_progress_speedup) max_progress_speedup = progress_speedup;
      }
    }
    return max_progress_speedup;
  }
  
  function min_progress_speedup(d) {
    var min_progress_speedup = 0;
    for(var i in d.progress_points) {
      for(var j in d.progress_points[i].measurements) {
        var progress_speedup = d.progress_points[i].measurements[j].progress_speedup;
        if(progress_speedup < min_progress_speedup) min_progress_speedup = progress_speedup;
      }
    }
    return min_progress_speedup;
  }
  
  var sort_functions = {
    alphabetical: function(a, b) {
      if(a.name > b.name) return 1;
      else return -1;
    },
    
    impact: function(a, b) {
      if(max_normalized_area(b) > max_normalized_area(a)) return 1;
      else return -1;
    },
    
    max_speedup: function(a, b) {
      if(max_progress_speedup(b) > max_progress_speedup(a)) return 1;
      else return -1;
    },
    
    min_speedup: function(a, b) {
      if(min_progress_speedup(a) > min_progress_speedup(b)) return 1;
      else return -1;
    }
  };
  
  this.draw = function(resize) {
    /****** Add or update divs to hold each plot ******/
    var plot_div_sel = container.selectAll('div')
      .data(this.getSpeedupData(minpoints_slider.getValue()), function(d) { return d.name; });
    
    var cols = 2;
    if(parseInt(d3.select('body').style('width'), 10) >= 1200) {
      cols = 3;
    }
    
    var plot_x_pos = function(d, i) {
      var col = i % cols;
      var plot_width = parseInt(plot_div_sel.style('width'), 10);
      return (col * plot_width) + 'px';
    }
    
    var plot_y_pos = function(d, i) {
      var row = (i - (i % cols)) / cols;
      return (row * 200) + 'px';
    }
    
    // First, remove divs that are disappearing
    plot_div_sel.exit().transition().duration(200)
      .style('opacity', 0).remove();
    
    // Insert new divs with zero opacity
    plot_div_sel.enter().append('div')
      .attr('class', 'col-xs-6 col-sm-6 col-md-6 col-lg-4')
      .style('margin-bottom', '-200px')
      .style('opacity', 0);
    
    // Sort plots by the chosen sorting function
    plot_div_sel.sort(sort_functions[d3.select('#sortby_field').node().value]);
    
    // Move divs into place. Only animate if we are not on a resizing redraw
    if(!resize) {
      plot_div_sel.transition().duration(400).delay(200)
        .style('top', plot_y_pos)
        .style('left', plot_x_pos)
        .style('opacity', 1);
    } else {
      plot_div_sel.style('left', plot_x_pos)
                  .style('top', plot_y_pos);
    }
    
    /****** Insert and remove plot titles ******/
    var plot_title_sel = plot_div_sel.selectAll('.plot-title').data([1]);
    plot_title_sel.enter().append('span').attr('class', 'plot-title');
    plot_title_sel.exit().remove();
    
    /****** Compute dimensions ******/
    var svg_width = parseInt(plot_div_sel.style('width'), 10);
    var svg_height = 150;
    var margins = {left: 55, right: 20, top: 10, bottom: 35};
    var plot_width = svg_width - margins.left - margins.right;
    var plot_height = svg_height - margins.top - margins.bottom;
    var radius = 3;
    var tick_size = 6;
    
    /****** Update scales ******/
    xscale.domain([0, 1]).range([0, plot_width]);
    yscale.domain([-1, 1]).range([plot_height, 0]);
    
    /****** Update axes ******/
    xaxis.tickSize(tick_size, 0, 0);
    yaxis.tickSize(tick_size, 0, 0);
    
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
    
    /****** Update plot titles ******/
    plot_div_sel.select('.plot-title').html(function(d) {
      return '<span class="smartpath">' + d.name + '</span>';
    });
    
    /****** Add or update plot areas ******/
    var plot_area_sel = plot_svg_sel.selectAll('g.plot_area').data([0]);
    plot_area_sel.enter().append('g').attr('class', 'plot_area');
    plot_area_sel.attr('transform', 'translate(' + margins.left + ', ' + margins.top + ')');
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
    xgrid_sel.attr('transform', 'translate(0, ' + plot_height + ')').call(xgrid);
    xgrid_sel.exit().remove();
    
    /****** Add or update y-grids ******/
    var ygrid_sel = plot_area_sel.selectAll('g.ygrid').data([0]);
    ygrid_sel.enter().append('g').attr('class', 'ygrid');
    ygrid_sel.call(ygrid);
    ygrid_sel.exit().remove();
  
    /****** Add or update x-axes ******/
    var xaxis_sel = plot_area_sel.selectAll('g.xaxis').data([0]);
    xaxis_sel.enter().append('g').attr('class', 'xaxis');
    xaxis_sel.attr('transform', 'translate(0, ' + plot_height + ')').call(xaxis);
    xaxis_sel.exit().remove();
    
    /****** Add or update x-axis titles ******/
    var xtitle_sel = plot_area_sel.selectAll('text.xtitle').data([0]);
    xtitle_sel.enter().append('text').attr('class', 'xtitle');
    xtitle_sel.attr('x', xscale(0.5))
              .attr('y', 32) // Approximate height of the x-axis
              .attr('transform', 'translate(0, ' + plot_height + ')')
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
    ytitle_sel.attr('x', -yscale(0)) // x and y are flipped because of rotation
              .attr('y', -50) // Approximate width of y-axis
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
      .data(function(d) { return d.progress_points; }, function(d) { return d.name; });
    series_sel.enter().append('g');
    series_sel.attr('class', function(d, k) { return 'series series' + (k%5); })
              .attr('style', 'clip-path: url(#clip);');
    series_sel.exit().remove();
  
    /****** Add or update trendlines ******/
    // Configure a loess smoother
    var loess = science.stats.loess()
      .bandwidth(0.4)
      .robustnessIterations(5);
    
    // Create an svg line to draw the loess curve
    var line = d3.svg.line().x(function(d) { return xscale(d[0]); })
                            .y(function(d) { return yscale(d[1]); })
                            .interpolate('basis');
  
    // Apply the loess smoothing to each series, then draw the lines
    var lines_sel = series_sel.selectAll('path').data(function(d) {
      var measurements = d.measurements.filter(function(e) { return e.progress_speedup >= -1 && e.progress_speedup < 1; });
      var xvals = measurements.map(function(e) { return e.speedup; });
      var yvals = measurements.map(function(e) { return e.progress_speedup; });
      
      if(xvals.length > 5) return [d3.zip(xvals, loess(xvals, yvals))];
      else return [d3.zip(xvals, yvals)];
    });
    lines_sel.enter().append('path');
    lines_sel.attr('d', line);
    lines_sel.exit().remove();
  
    /****** Add or update points ******/
    var points_sel = series_sel.selectAll('circle').data(function(d) { return d.measurements; });
    points_sel.enter().append('circle').attr('r', radius);
    points_sel.attr('cx', function(d) { return xscale(d.speedup); })
              .attr('cy', function(d) { return yscale(d.progress_speedup); })
              .on('mouseover', function(d, i) {
                d3.select(this).classed('highlight', true);
                tip.show(d, i);
              })
              .on('mouseout', function(d, i) {
                d3.select(this).classed('highlight', false);
                tip.hide(d, i);
              });
    points_sel.exit().remove();
    
    /****** Add or update legend ******/
    var legend_sel = d3.select('#legend');
    
    var legend_entries_sel = legend_sel.selectAll('p.legend-entry').data(this.getProgressPoints());
    legend_entries_sel.enter().append('p').attr('class', 'legend-entry');
    legend_entries_sel.classed('noseries', false)
      .html(function(d, i) { 
        return '<i class="fa fa-circle series' + (i % 4) + '"></i> <span class="smartpath">' + d + '</span>';
      });
    legend_entries_sel.exit().remove();
    
    /****** Shorten paths ******/
    var smartpaths = d3.selectAll('.smartpath');
    smartpaths.classed('smartpath', false)
              .html(function() {
                var current = d3.select(this);
                var parts = current.text().split('/');
                var filename = parts[parts.length-1];
                if(parts.length > 3) {
                  current.attr('title', parts.join('/'));
                  return '&hellip;/'+filename;
                } else {
                  return parts.join('/');
                }
              });
  }
}

var current_profile = new Profile(d3.select('#plot-area'));

function parseLine(s) {
  if(s[0] == '{') {
    return JSON.parse(s);
  } else {
    var parts = s.split('\t');
    var obj = { type: parts[0] };
    for(var i=0; i<parts.length; i++) {
      var equals_index = parts[i].indexOf('=');
      if(equals_index == -1) continue;
      var key = parts[i].substring(0, equals_index);
      var value = parts[i].substring(equals_index+1);
      
      if(key == 'type' && obj.type == 'progress-point') {
        key = 'point-type';
      } else if(key == 'delta' || key == 'time' || key == 'duration') {
        value = parseInt(value);
      } else if(key == 'speedup') {
        value = parseFloat(value);
      }
      
      obj[key] = value;
    }
    return obj;
  }
}

// Parse a profile
function parseProfile(s) {
  var p = new Profile(d3.select('#plot-area'));
  var lines = s.split('\n');
  
  var experiment = undefined;
  
  for(var i=0; i<lines.length; i++) {
    if(lines[i].length == 0) continue;
    var entry = parseLine(lines[i]);
    
    if(entry.type == 'experiment') {
      experiment = entry;
    } else if(entry.type == 'progress-point') {
      p.addProgressMeasurement(experiment.selected, entry.name, experiment.speedup, entry.delta, experiment.duration);
    }
  }
  
  current_profile = p;
  current_profile.draw();
}

// Set a handler for the load profile button
d3.select('#load-profile-btn').on('click', function() {
  // Reset the filename field
  d3.select('#load-profile-filename').attr('value', '');
  
  // Disable the open button
  d3.select('#load-profile-open-btn').classed('disabled', true);
});

// Set a handler for the fake browse button
d3.select('#load-profile-browse-btn').on('click', function() {
  $('#load-profile-file').trigger('click');
});

// Set a handler for file selection
d3.select('#load-profile-file').on('change', function() {
  var file_browser = this;
  var open_button = d3.select('#load-profile-open-btn');
  
  d3.select('#load-profile-filename').attr('value', file_browser.value.replace(/C:\\fakepath\\/i, ''));
  
  d3.select('#sortby_field').attr('disabled', null);
  
  open_button.classed('disabled', false)
    .on('click', function() {
      var reader = new FileReader();
      reader.onload = function(event) {
        var contents = event.target.result;
        parseProfile(contents);
      };

      reader.onerror = function(event) {
        console.error("Unable to read file. Error code: " + event.target.error.code);
      };
      
      // Read the profile
      reader.readAsText(file_browser.files[0]);
      
      // Clear the file browser value
      file_browser.value = '';
    });
});

minpoints_slider.on('slideStop', function() {
  if(current_profile) current_profile.draw();
});

d3.select('#sortby_field').on('change', function() {
  if(current_profile) current_profile.draw();
});

d3.select(window).on('resize', function() {
  if(current_profile) current_profile.draw(true);
});

/*var files = {
  name: '/',
  children: [
    { name: 'I',
      children: [
        { name: 'A' },
        { name: 'B' },
        { name: 'C',
          children: [
            { name: 'i' },
            { name: 'ii' }
          ]
        }
      ]
    },
    { name: 'II',
      children: [
        { name: 'D' },
        { name: 'E',
          children: [
            { name: 'iii' },
            { name: 'iv' }
          ]
        }
      ]
    }
  ]
};

d3.select('#file-area').selectAll('ul').data([files])
  .enter().append('ul').call(recurse);

function recurse(sel) {
  sel.text(function(d) { return d.name; });
  sel.each(function(d) {
    if(d.children) {
      d3.select(this).append('ul').selectAll('li').data(d.children)
        .enter().append('li').call(recurse);
    }
  });
}*/
