// Ensure the brower supports the File API
if (!(<any> window).File || !(<any> window).FileReader) {
  alert('The File APIs are not fully supported in this browser.');
}

let current_profile: Profile = undefined;

function get_min_points(): number {
  return (<any> d3.select('#minpoints_field').node()).value;
}

// Returns the minimum number of path parts to include starting from the
// end of the path, in order for the resulting string to be unique.
function get_minimum_parts_for_unique_path(paths: string[]): number {
  if (paths.length <= 1) {
    return 1;
  }

  let minimum = 1;
  let shortest_parts = Infinity;
  let is_unique = false;

  // Remove line numbers from paths
  paths = paths.map(path => path.replace(/:[0-9]*/, ''));

  // Remove duplicate fully qualified path names
  paths = remove_duplicates(paths);

  // Special case: all of the paths are the same file. In that case, then we
  // only need to return the file name.
  const all_identical_paths = paths.every(path => path == paths[0]);
  if (all_identical_paths) {
    return 1;
  }

  while (true) {
    const trimmed_paths = paths
      .map(path => {
        const parts: string[] = path.split('/');
        shortest_parts = Math.min(shortest_parts, parts.length);
        return parts.slice(parts.length - minimum, parts.length).join('/');
      })
    
    is_unique = !has_duplicates(trimmed_paths);

    if (is_unique) {
      return minimum;
    } else if (minimum >= shortest_parts) {
      // We can't possibly return a minimum parts needed that is greater than
      // the smallest parts possible
      return shortest_parts;
    } else {
      minimum += 1;
    }
  }
}

// Returns the last number of parts of a slash-separated path.
function get_last_path_parts(num_parts: number, path: string) {
  // Just return the path if it does not have any slash-separated parts
  if (path.indexOf('/') === -1) {
    return path;
  }
  const parts = path.split('/');
  return parts.slice(parts.length - num_parts, parts.length).join('/');
}

// This could be made simpler by using ES2015 Set instead.
function has_duplicates(array: string[]) {
  var seen = Object.create(null);
  for (let value of array) {
    if (value in seen) {
      return true;
    }
    seen[value] = true;
  }
  return false;
}

function remove_duplicates(array: string[]) {
  var uniq: {[key: string]: string} = {};

  for (let value of array) {
    uniq[value + '::' + typeof value] = value;
  }

  return Object.keys(uniq).map(key => uniq[key]);
}

function display_warning(title: string, text: string): void {
  const warning = $(
    `<div class="alert alert-warning alert-dismissible" role="alert">
      <button type="button" class="close" data-dismiss="alert" aria-label="Close"><span aria-hidden="true">&times;</span></button>
      <strong>${title}:</strong> ${text}
    </div>`);
  $('#warning-area').append(warning);

  // Fade out after 5 seconds.
  setTimeout(() => {
    warning.fadeOut(500, () => {
      warning.alert('close');
    });
  }, 5000);
}

function update(resize?: boolean) {
  if (current_profile === undefined) return;

  // Enable the sortby field
  d3.select('#sortby_field').attr('disabled', null);

  // Draw plots
  let num_plots = current_profile.drawPlots(resize);
  
  // Clear the message area
  d3.select('#plot-message').text('');
  
  // Display a warning if there are no plots
  if (num_plots == 0) {
    d3.select('#plot-message').html('<h1>No Data to Plot</h1><p>Your profile does not contain enough observations to generate any plots. Try reducing the minimum number of points required to show a plot using the slider, or run your program for a longer time to collect more data.</p>');
  }

  // Draw the legend
  current_profile.drawLegend();

  let tooltip = d3.select("body")
  	.append("div")
  	.style("position", "absolute")
  	.style("z-index", "10")
  	.style("visibility", "hidden");

  let all_paths: string[] = [];

  // Collect all of the paths that we have, in order to calculate what the
  // common path prefix is among all of the paths.
  d3.selectAll('.path').text(path => {
    // Filter out any paths which do not contain slash-separated parts
    if (path.indexOf('/') !== -1) {
      all_paths.push(path)
    }
    return path;
  });
  
  let minimum_parts = get_minimum_parts_for_unique_path(all_paths);

  // Shorten path strings
  let paths = d3.selectAll('.path')
    .classed('path', false).classed('shortpath', true)
    .text((path: string) => get_last_path_parts(minimum_parts, path))
    .attr('title', (datum: string, index: number, outerIndex: number) => {
      return datum;
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
  let file_browser = this;
  let open_button = d3.select('#load-profile-open-btn');

  d3.select('#load-profile-filename').attr('value', file_browser.value.replace(/C:\\fakepath\\/i, ''));

  open_button.classed('disabled', false)
    .on('click', function() {
      var reader = new FileReader();
      reader.onload = function(event) {
        let contents: string = (<any> event.target).result;
        current_profile = new Profile(contents, d3.select('#plot-area'), d3.select('#legend'), get_min_points, display_warning);
        update();
      };

      reader.onerror = function(event) {
        console.error("Unable to read file. Error code: " + (<any> event.target).error.code);
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

let sample_profiles = ['blackscholes', 'dedup', 'ferret', 'fluidanimate', 'sqlite', 'swaptions'];
let sample_profile_objects: {[name: string]: Profile} = {};

let samples_sel = d3.select('#samples').selectAll('.sample-profile').data(sample_profiles)
  .enter().append('button')
    .attr('class', 'btn btn-sm btn-default sample-profile')
    .attr('data-dismiss', 'modal')
    .attr('loaded', 'no')
    .text(function(d) { return d; })
    .on('click', function(d) {
      let sel = d3.select(this);
      if (sel.attr('loaded') !== 'yes') {
        // Avoid race condition: Set first.
        sel.attr('loaded', 'yes');
        const xhr = new XMLHttpRequest();
        xhr.open('GET', `profiles/${d}.coz`);
        xhr.onload = function() {
          current_profile = sample_profile_objects[d] =
            new Profile(xhr.responseText, d3.select('#plot-area'), d3.select('#legend'), get_min_points, display_warning);
          update();
        };
        xhr.onerror = function() {
          sel.attr('loaded', 'no');
          display_warning("Error", `Failed to load profile for ${d}.`);
        };
        xhr.send();
      } else {
        current_profile = sample_profile_objects[d];
        update();
      }
    });
