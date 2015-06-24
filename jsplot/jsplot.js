// Ensure the brower supports the File API
if (!window.File || !window.FileReader) {
  alert('The File APIs are not fully supported in this browser.');
}

var current_profile = undefined;

function update(resize) {
  if(current_profile == undefined) return;
  
  var min_points = d3.select('#minpoints_field').node().value;
  
  current_profile.drawPlots(d3.select('#plot-area'), min_points, resize);
  current_profile.drawLegend(d3.select('#legend'));
  
  // Shorten path strings
  var paths = d3.selectAll('.path');
  paths.attr('class', 'shortpath')
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
        current_profile = new Profile(contents);
        update();
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

// Update the plots and minpoints display when dragged or clicked
d3.select('#minpoints_field').on('input', function() {
  d3.select('#minpoints_display').text(this.value);
  update();
});

d3.select('#sortby_field').on('change', update);

d3.select(window).on('resize', function() { update(true); });

var sample_profiles = ['blackscholes', 'dedup', 'ferret', 'fluidanimate', 'sqlite', 'swaptions'];

var samples_sel = d3.select('#samples').selectAll('.sample-profile').data(sample_profiles)
  .enter().append('button')
    .attr('class', 'btn btn-sm btn-default sample-profile')
    .attr('data-dismiss', 'modal')
    .attr('loaded', 'no')
    .text(function(d) { return d; })
    .on('click', function(d) {
      var sel = d3.select(this);
      if(sel.attr('loaded') != 'yes') {
        d3.select('body').append('script')
          .attr('src', 'profiles/' + d + '.coz.js')
          .on('load', function() {
            sel.attr('loaded', 'yes');
            current_profile = eval(d + '_profile');
            update();
          });
      } else {
        current_profile = eval(d + '_profile');
        update();
      }
    });
