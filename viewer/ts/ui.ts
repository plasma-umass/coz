// Ensure the brower supports the File API
if (!(<any> window).File || !(<any> window).FileReader) {
  alert('The File APIs are not fully supported in this browser.');
}

let current_profile: Profile = undefined;

function get_min_points(): number {
  return (<any> d3.select('#minpoints_field').node()).value;
}

// Returns the longest common path prefix between all strings in an array
// Ex: get_common_path_prefix(["/a/b/c/", "/a/b/d", "/a/e/d/"]) => "/a/"
// Ex: get_common_path_prefix(["/a/b/test.c:44", "/a/b/util.c:123"]) => "/a/b/"
function get_common_path_prefix(paths: string[]): string {
  if (paths.length === 1) {
    return "";
  }

  const A = paths.concat().sort();

  const shortest = paths.reduce((min, str) => min < str ? min : str, paths[0]);
  const longest = paths.reduce((min, str) => min > str ? min : str, paths[0]);
  const last_slash_index = shortest.lastIndexOf("/");

  let i = 0;
  let end = shortest.length;

  // If there is a last slash, then we will stop there, so that we do not
  // cut off the name of the file.
  if (last_slash_index !== -1) {
    end = last_slash_index + 1;
  }

  while (i < end && shortest.charAt(i) === longest.charAt(i)) {
    i++;
  }

  let prefix = shortest.substring(0, i);

  // Check if the string slicing produced a path with a cutoff word.
  // Ex: "/a/b/c/some_path" => "/a/b/c/some"
  // We want to turn that into "/a/b/c/" in that case.
  let last_slash = prefix.lastIndexOf("/");
  if (last_slash !== -1 && last_slash < prefix.length - 1) {
    prefix = prefix.substring(0, last_slash + 1);
  }
  
  return prefix
}

// Returns a set of the common path prefixes among a set of strings. The number
// of prefixes is determined by how many unique path prefixes there are within
// the first slash.
// Ex: get_common_path_prefixes([
//    "file.c:123",
//    "file.c:123",
//    "a/file.c:123",
//    "a/file.c:123",
//    "/test/a/b/c/d/file.c:123",
//    "/test/a/b/c/f/file.c:123",
//    "/other/a/b/c/d/file.c:123",
//    "/other/a/c/file.c:321",
//    "/a/b/c/file.c:100",
//    "/a/b/d/file.c:123",
// ]) => ["/test/a/b/c/", "/other/a/", "/a/b/"]
function get_common_path_prefixes(paths: string[]): string[] {
  // Paths grouped by the first path part
  const grouped_paths: {[s: string]: string[]} = {};

  for (let path of paths) {
    const first_slash = path.indexOf("/");
    const second_slash = path.indexOf("/", first_slash+1);
    const has_leading_slash = first_slash === 0;
    const has_second_slash = second_slash !== -1;

    // Includes paths that have two slashes like:
    // * /a/b/c/file.c:123
    // * /a/file.c:123
    // * a/b/file.c:123
    // Does not include paths like:
    // * a/file.c:123
    // * file.c:123
    // * /file.c:123
    if (has_second_slash) {
      const parts = path.split('/');

      // Includes paths that have a leading slash like:
      // * /a/b/c/file.c:123
      // * /a/file.c:123
      // Does not include paths like:
      // * a/b/file.c:123
      let initial_prefix = parts[0];

      if (has_leading_slash) {
        // If path is "/a/b/c/file.c:123", then prefix is "/a"
        initial_prefix = "/" + parts[1];
      }

      if (grouped_paths[initial_prefix] === undefined) {
        grouped_paths[initial_prefix] = [path];
      } else {
        grouped_paths[initial_prefix].push(path);
      }
    }
  }

  // Get the largest common path prefix for each different group of paths
  const common_prefixes = [];
  for (let prefix in grouped_paths) {
    let paths = grouped_paths[prefix];
    common_prefixes.push(get_common_path_prefix(paths));
  }

  return common_prefixes;
}

{
  const common_path_prefixes = get_common_path_prefixes([
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
function get_unique_path_part(common_paths: string[], path: string): string {
  // For the same reasons as the common path collection logic, we will
  // not shorten any paths which do not contain a slash, which is hopefully
  // just paths that are only a file name.
  for (let common_path of common_paths) {
    // Check if the prefix applies to this path
    if (path.indexOf(common_path) !== -1) {
      return path.substr(common_path.length, path.length)
    }
  }
  return path;
}

{
  const test_data = [
    "/home/fitzgen/walrus/src/lib.rs",
    "/home/fitzgen/walrus/src/passes/mod.rs",
    "/home/fitzgen/rayon/src/lib.rs",
    "/home/fitzgen/rayon/src/spawner/mod.rs"
  ];

  let common_paths = get_common_path_prefixes(test_data);
  console.log(common_paths);
  console.log(common_paths[0] === "/home/fitzgen/");
  console.log(get_unique_path_part(common_paths, "/home/fitzgen/walrus/src/lib.rs") === "walrus/src/lib.rs");
  console.log(get_unique_path_part(common_paths, "/home/fitzgen/rayon/src/lib.rs") === "rayon/src/lib.rs");
}

{
  const test_data = [
    "/a/b/c/test.cpp:135",
    "/a/b/c/test.cpp:145",
    "/a/b/c/test.cpp:251",
  ];

  let common_paths = get_common_path_prefixes(test_data);
  console.log(common_paths);
  console.log(common_paths[0] === "/a/b/c/");
  console.log(get_unique_path_part(common_paths, "/a/b/c/test.cpp:135") === "test.cpp:135");
  console.log(get_unique_path_part(common_paths, "/a/b/c/d/util.c:12") === "d/util.c:12");
  console.log(get_unique_path_part(common_paths, "/a/b/c/test.cpp:23094345") === "test.cpp:23094345");
}

{
  const test_data = [
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
  ]

  let common_paths = get_common_path_prefixes(test_data);
  console.log(common_paths);
  console.log(common_paths.indexOf("/rustc/eae3437dfe991621e8afdc82734f4a172d7ddf9b/src/") !== -1);
  console.log(common_paths.indexOf("/home/cmchenry/.cargo/registry/src/github.com-1ecc6299db9ec823/") !== -1);
  console.log(common_paths.indexOf("src/") === -1);
  console.log(get_unique_path_part(common_paths, "/rustc/eae3437dfe991621e8afdc82734f4a172d7ddf9b/src/libcore/slice/mod.rs:3261") === "libcore/slice/mod.rs:3261");
  console.log(get_unique_path_part(common_paths, "/home/cmchenry/.cargo/registry/src/github.com-1ecc6299db9ec823/memchr-2.2.1/src/x86/sse2.rs:0") === "memchr-2.2.1/src/x86/sse2.rs:0");
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
  d3.selectAll('.path')
    .text((path: string) => {
      // Do not consider any paths which do not contain a slash as a way of
      // filtering out paths which are already just the file name.
      if (path.indexOf("/") !== -1) {
        all_paths.push(path)
      }
      return path;
    });
  
  let common_path_prefixes = get_common_path_prefixes(all_paths);

  // Shorten path strings
  let paths = d3.selectAll('.path')
    .classed('path', false).classed('shortpath', true)
    .text((path: string) => get_unique_path_part(common_path_prefixes, path))
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
