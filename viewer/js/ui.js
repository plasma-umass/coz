// Ensure the browser supports the File API
if (!window.File || !window.FileReader) {
    alert('The File APIs are not fully supported in this browser.');
}
let current_profile = undefined;
function get_min_points() {
    return d3.select('#minpoints_field').node().value;
}
// Returns the minimum number of path parts to include starting from the
// end of the path, in order for the resulting string to be unique.
function get_minimum_parts_for_unique_path(paths) {
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
            const parts = path.split('/');
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
    const parts = path.split('/');
    return parts.slice(parts.length - num_parts, parts.length).join('/');
}
// This could be made simpler by using ES2015 Set instead.
function has_duplicates(array) {
    var seen = Object.create(null);
    for (let value of array) {
        if (value in seen) {
            return true;
        }
        seen[value] = true;
    }
    return false;
}
function remove_duplicates(array) {
    var uniq = {};
    for (let value of array) {
        uniq[value + '::' + typeof value] = value;
    }
    return Object.keys(uniq).map(key => uniq[key]);
}
function display_warning(title, text) {
    const warning = $(`<div class="alert alert-warning alert-dismissible" role="alert">
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
function display_success(title, text) {
    const success = $(`<div class="alert alert-success alert-dismissible" role="alert" style="background: rgba(16, 185, 129, 0.15); color: #10b981; border-left: 4px solid #10b981;">
      <button type="button" class="close" data-dismiss="alert" aria-label="Close"><span aria-hidden="true">&times;</span></button>
      <strong>${title}:</strong> ${text}
    </div>`);
    $('#warning-area').append(success);
    // Fade out after 3 seconds.
    setTimeout(() => {
        success.fadeOut(500, () => {
            success.alert('close');
        });
    }, 3000);
}
function update(resize) {
    if (current_profile === undefined)
        return;
    // Enable the sortby field
    d3.select('#sortby_field').attr('disabled', null);
    // Draw plots
    let num_plots = current_profile.drawPlots(resize);
    // Clear the message area
    d3.select('#plot-message').text('');
    // Display a warning if there are no plots
    if (num_plots == 0) {
        d3.select('#plot-message').html(`
      <div class="welcome-container">
        <div class="welcome-icon" style="color: var(--accent-color);">&#9888;</div>
        <h1>No Data to Plot</h1>
        <p class="welcome-subtitle">Your profile doesn't contain enough observations to generate plots.</p>
        <div class="getting-started" style="max-width: 500px; margin: 2rem auto;">
          <h4 style="text-align: center; margin-bottom: 1rem;">Try these solutions:</h4>
          <ul style="color: var(--text-secondary); text-align: left;">
            <li>Reduce the minimum points using the slider</li>
            <li>Run your program longer to collect more data</li>
            <li>Ensure progress points are being hit during execution</li>
          </ul>
        </div>
      </div>
    `);
    }
    // Draw the legend
    current_profile.drawLegend();
    let tooltip = d3.select("body")
        .append("div")
        .style("position", "absolute")
        .style("z-index", "10")
        .style("visibility", "hidden");
    let all_paths = [];
    // Collect all of the paths that we have, in order to calculate what the
    // common path prefix is among all of the paths.
    d3.selectAll('.path').text(path => {
        // Filter out any paths which do not contain slash-separated parts
        if (path.indexOf('/') !== -1) {
            all_paths.push(path);
        }
        return path;
    });
    let minimum_parts = get_minimum_parts_for_unique_path(all_paths);
    // Shorten path strings
    let paths = d3.selectAll('.path')
        .classed('path', false).classed('shortpath', true)
        .text((path) => get_last_path_parts(minimum_parts, path))
        .attr('title', (datum, index, outerIndex) => {
        return datum;
    });
}
// Load a profile from file contents
function loadProfileFromText(contents, filename) {
    current_profile = new Profile(contents, d3.select('#plot-area'), d3.select('#legend'), get_min_points, display_warning);
    // Auto-reduce minimum points if needed to show at least one graph
    autoAdjustMinPoints();
    update();
    if (filename) {
        display_success('Profile Loaded', `Successfully loaded ${filename}`);
    }
}
// Automatically reduce minimum points slider until at least one graph appears
function autoAdjustMinPoints() {
    if (!current_profile)
        return;
    const slider = document.getElementById('minpoints_field');
    if (!slider)
        return;
    const maxValue = parseInt(slider.max, 10);
    let currentValue = parseInt(slider.value, 10);
    // Check if current setting produces plots
    let plotCount = current_profile.countPlots(currentValue);
    if (plotCount > 0) {
        // Already have plots, no adjustment needed
        return;
    }
    // Try reducing minimum points until we get at least one plot
    for (let minPoints = currentValue - 1; minPoints >= 0; minPoints--) {
        plotCount = current_profile.countPlots(minPoints);
        if (plotCount > 0) {
            // Found a value that works - update the slider
            slider.value = String(minPoints);
            d3.select('#minpoints_display').text(String(minPoints));
            display_warning('Auto-adjusted', `Reduced minimum points to ${minPoints} to display available data`);
            return;
        }
    }
    // No value produces plots - the profile truly has no plottable data
}
// Handle file loading from a File object
function handleFileLoad(file) {
    if (!file.name.endsWith('.coz')) {
        display_warning('Invalid File', 'Please select a .coz profile file');
        return;
    }
    const reader = new FileReader();
    reader.onload = function (event) {
        let contents = event.target.result;
        loadProfileFromText(contents, file.name);
    };
    reader.onerror = function (event) {
        display_warning('Error', 'Unable to read file. Error code: ' + event.target.error.code);
    };
    reader.readAsText(file);
}
// Setup drag and drop functionality
function setupDropZone(element, onDrop) {
    // Prevent default drag behaviors
    ['dragenter', 'dragover', 'dragleave', 'drop'].forEach(eventName => {
        element.addEventListener(eventName, (e) => {
            e.preventDefault();
            e.stopPropagation();
        }, false);
    });
    // Highlight drop zone when dragging over it
    ['dragenter', 'dragover'].forEach(eventName => {
        element.addEventListener(eventName, () => {
            element.classList.add('drag-over');
        }, false);
    });
    ['dragleave', 'drop'].forEach(eventName => {
        element.addEventListener(eventName, () => {
            element.classList.remove('drag-over');
        }, false);
    });
    // Handle dropped files
    element.addEventListener('drop', (e) => {
        const files = e.dataTransfer ? e.dataTransfer.files : null;
        if (files && files.length > 0) {
            onDrop(files[0]);
        }
    }, false);
}
// Setup the main drop zone on the welcome page
const mainDropZone = document.getElementById('drop-zone');
if (mainDropZone) {
    setupDropZone(mainDropZone, (file) => {
        handleFileLoad(file);
    });
    // Click to browse
    mainDropZone.addEventListener('click', () => {
        const input = document.getElementById('drop-zone-input');
        if (input) {
            input.click();
        }
    });
    const dropZoneInput = document.getElementById('drop-zone-input');
    if (dropZoneInput) {
        dropZoneInput.addEventListener('change', () => {
            if (dropZoneInput.files && dropZoneInput.files.length > 0) {
                handleFileLoad(dropZoneInput.files[0]);
                dropZoneInput.value = '';
            }
        });
    }
}
// Setup modal drop zone
const modalDropZone = document.getElementById('modal-drop-zone');
if (modalDropZone) {
    setupDropZone(modalDropZone, (file) => {
        handleFileLoad(file);
        // Close the modal
        $('#load-profile-dlg').modal('hide');
    });
}
// Setup global drag and drop on the entire page
document.body.addEventListener('dragover', (e) => {
    e.preventDefault();
}, false);
document.body.addEventListener('drop', (e) => {
    e.preventDefault();
    const files = e.dataTransfer ? e.dataTransfer.files : null;
    if (files && files.length > 0 && files[0].name.endsWith('.coz')) {
        handleFileLoad(files[0]);
    }
}, false);
// Set a handler for file selection - immediately load the file
d3.select('#load-profile-file').on('change', function () {
    let file_browser = this;
    if (file_browser.files && file_browser.files.length > 0) {
        handleFileLoad(file_browser.files[0]);
        file_browser.value = '';
        // Close the modal
        $('#load-profile-dlg').modal('hide');
    }
});
// Update the plots and minpoints display when dragged or clicked
d3.select('#minpoints_field').on('input', function () {
    d3.select('#minpoints_display').text(this.value);
    update();
});
d3.select('#sortby_field').on('change', update);
// AI provider toggle
$('#ai-provider').on('change', function () {
    toggleProviderFields();
    saveLLMSettings();
});
// Refresh models button
$('#ai-model-refresh').on('click', function () {
    let provider = getSelectedProvider();
    try {
        localStorage.removeItem('coz-models-' + provider);
    }
    catch (e) { }
    _provider_models[provider] = [];
    _force_model_refresh = true;
    if (provider === 'anthropic')
        _anthropic_models_fetched = false;
    else if (provider === 'openai')
        _openai_models_fetched = false;
    else if (provider === 'bedrock')
        _bedrock_models_fetched = false;
    else if (provider === 'ollama')
        _ollama_models_fetched = false;
    populateModelDropdown();
    // Spin the icon briefly
    let icon = $(this).find('i');
    icon.addClass('fa-spin');
    setTimeout(function () { icon.removeClass('fa-spin'); }, 1000);
});
// Re-fetch Ollama models when host changes
$('#ai-ollama-host').on('change', function () {
    _ollama_models_fetched = false;
    _provider_models['ollama'] = [];
    if (getSelectedProvider() === 'ollama') {
        populateModelDropdown();
    }
    saveLLMSettings();
});
// Re-fetch Bedrock models when region or credentials change
$('#ai-bedrock-region, #ai-aws-access-key, #ai-aws-secret-key, #ai-aws-session-token').on('change', function () {
    _bedrock_models_fetched = false;
    if (getSelectedProvider() === 'bedrock') {
        populateModelDropdown();
    }
    saveLLMSettings();
});
// Save model selection on change
$('#ai-model').on('change', function () {
    saveLLMSettings();
});
// Save API key on blur and re-fetch models for Anthropic/OpenAI
$('#ai-api-key').on('change', function () {
    let provider = getSelectedProvider();
    if (provider === 'anthropic') {
        _anthropic_models_fetched = false;
        _provider_models['anthropic'] = [];
    }
    else if (provider === 'openai') {
        _openai_models_fetched = false;
        _provider_models['openai'] = [];
    }
    populateModelDropdown();
    saveLLMSettings();
});
// API key show/hide toggle
$('#ai-key-toggle').on('click', function () {
    let keyInput = document.getElementById('ai-api-key');
    let icon = document.querySelector('#ai-key-toggle i');
    if (keyInput && icon) {
        if (keyInput.type === 'password') {
            keyInput.type = 'text';
            icon.className = 'fa fa-eye-slash';
        }
        else {
            keyInput.type = 'password';
            icon.className = 'fa fa-eye';
        }
    }
});
// AWS credential show/hide toggles
$(document).on('click', '.aws-key-toggle', function () {
    let input = $(this).siblings('input')[0];
    let icon = $(this).find('i')[0];
    if (input && icon) {
        if (input.type === 'password') {
            input.type = 'text';
            icon.className = 'fa fa-eye-slash';
        }
        else {
            input.type = 'password';
            icon.className = 'fa fa-eye';
        }
    }
});
d3.select(window).on('resize', function () { update(true); });
// Theme toggle handler
const themeToggle = document.getElementById('theme-toggle');
if (themeToggle) {
    // Load saved preference, or use time-of-day default
    const savedTheme = localStorage.getItem('coz-theme');
    let useDark;
    if (savedTheme) {
        useDark = savedTheme !== 'light';
    }
    else {
        // Default: dark between 7pm and 7am, light otherwise
        const hour = new Date().getHours();
        useDark = hour >= 19 || hour < 7;
    }
    if (!useDark) {
        document.documentElement.classList.add('light-mode');
        themeToggle.checked = false;
    }
    else {
        themeToggle.checked = true;
    }
    themeToggle.addEventListener('change', () => {
        if (themeToggle.checked) {
            // Dark mode
            document.documentElement.classList.remove('light-mode');
            localStorage.setItem('coz-theme', 'dark');
        }
        else {
            // Light mode
            document.documentElement.classList.add('light-mode');
            localStorage.setItem('coz-theme', 'light');
        }
    });
}
// Sidebar resize handle
(function setupSidebarResize() {
    let handle = document.getElementById('sidebar-resize-handle');
    let sidebar = document.getElementById('sidebar');
    if (!handle || !sidebar)
        return;
    let mainContent = document.querySelector('.main-content');
    if (!mainContent)
        return;
    let isDragging = false;
    function positionHandle() {
        let sidebarRect = sidebar.getBoundingClientRect();
        handle.style.left = sidebarRect.width + 'px';
    }
    function applySidebarWidth(width) {
        let clamped = Math.max(150, Math.min(width, window.innerWidth - 200));
        sidebar.style.width = clamped + 'px';
        mainContent.style.marginLeft = clamped + 'px';
        positionHandle();
    }
    // Restore saved width
    let savedWidth = localStorage.getItem('coz-sidebar-width');
    if (savedWidth) {
        applySidebarWidth(parseInt(savedWidth, 10));
    }
    else {
        // Position handle at default sidebar width
        setTimeout(positionHandle, 0);
    }
    handle.addEventListener('mousedown', function (e) {
        e.preventDefault();
        isDragging = true;
        handle.classList.add('dragging');
        document.body.style.cursor = 'col-resize';
        document.body.style.userSelect = 'none';
    });
    document.addEventListener('mousemove', function (e) {
        if (!isDragging)
            return;
        applySidebarWidth(e.clientX);
    });
    document.addEventListener('mouseup', function () {
        if (!isDragging)
            return;
        isDragging = false;
        handle.classList.remove('dragging');
        document.body.style.cursor = '';
        document.body.style.userSelect = '';
        let currentWidth = sidebar.getBoundingClientRect().width;
        localStorage.setItem('coz-sidebar-width', String(Math.round(currentWidth)));
        update(true);
    });
    // Double-click to reset
    handle.addEventListener('dblclick', function () {
        sidebar.style.width = '';
        mainContent.style.marginLeft = '';
        localStorage.removeItem('coz-sidebar-width');
        setTimeout(function () {
            positionHandle();
            update(true);
        }, 0);
    });
    // Reposition on window resize
    window.addEventListener('resize', positionHandle);
})();
// Sample profiles configuration
let sample_profiles = ['blackscholes', 'dedup', 'ferret', 'fluidanimate', 'sqlite', 'swaptions'];
let sample_profile_objects = {};
// Create sample profile buttons for the modal
let samples_sel = d3.select('#samples').selectAll('.sample-profile').data(sample_profiles)
    .enter().append('button')
    .attr('class', 'btn btn-sm sample-profile')
    .attr('loaded', 'no')
    .text(function (d) { return d; })
    .on('click', function (d) {
    loadSampleProfile(d, d3.select(this), true);
});
// Create quick sample buttons for the welcome page
let quick_samples_sel = d3.select('#quick-samples').selectAll('.sample-profile').data(sample_profiles)
    .enter().append('button')
    .attr('class', 'btn btn-sm sample-profile')
    .attr('loaded', 'no')
    .text(function (d) { return d; })
    .on('click', function (d) {
    loadSampleProfile(d, d3.select(this));
});
// Function to load a sample profile
function loadSampleProfile(name, sel, closeModal) {
    if (sel.attr('loaded') !== 'yes') {
        // Show loading state
        sel.text('Loading...').attr('disabled', 'true');
        // Avoid race condition: Set first.
        sel.attr('loaded', 'yes');
        const xhr = new XMLHttpRequest();
        xhr.open('GET', `profiles/${name}.coz`);
        xhr.onload = function () {
            current_profile = sample_profile_objects[name] =
                new Profile(xhr.responseText, d3.select('#plot-area'), d3.select('#legend'), get_min_points, display_warning);
            update();
            display_success('Sample Loaded', `Loaded ${name} profile`);
            // Reset button state
            sel.text(name).attr('disabled', null);
            // Close the modal if requested
            if (closeModal) {
                $('#load-profile-dlg').modal('hide');
            }
        };
        xhr.onerror = function () {
            sel.attr('loaded', 'no');
            sel.text(name).attr('disabled', null);
            display_warning("Error", `Failed to load profile for ${name}.`);
        };
        xhr.send();
    }
    else {
        current_profile = sample_profile_objects[name];
        update();
        // Close the modal if requested
        if (closeModal) {
            $('#load-profile-dlg').modal('hide');
        }
    }
}
// Add keyboard shortcut for opening file dialog and help
document.addEventListener('keydown', (e) => {
    // Ctrl/Cmd + O to open file dialog
    if ((e.ctrlKey || e.metaKey) && e.key === 'o') {
        e.preventDefault();
        $('#load-profile-dlg').modal('show');
    }
    // ? key to show help modal (when not typing in an input)
    if (e.key === '?' && !(e.target instanceof HTMLInputElement || e.target instanceof HTMLTextAreaElement)) {
        e.preventDefault();
        $('#help-modal').modal('show');
    }
});
// Check for ?load= query parameter and auto-load profile
function checkAutoLoad() {
    const params = new URLSearchParams(window.location.search);
    const loadFile = params.get('load');
    if (loadFile) {
        const xhr = new XMLHttpRequest();
        xhr.open('GET', loadFile);
        xhr.onload = function () {
            if (xhr.status === 200) {
                loadProfileFromText(xhr.responseText, loadFile);
            }
            else {
                display_warning('Error', `Failed to load profile: ${loadFile}`);
            }
        };
        xhr.onerror = function () {
            display_warning('Error', `Failed to load profile: ${loadFile}`);
        };
        xhr.send();
    }
}
// Auto-load on page ready
document.addEventListener('DOMContentLoaded', checkAutoLoad);
//# sourceMappingURL=ui.js.map