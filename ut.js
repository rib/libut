"use strict";

//# sourceURL=ut.js

/*
 * TODO:
 *
 * - introduce a left margin size for process + thread labels
 * - insert axis for details
 * - align process/thread names with trace
 * - test with trace that has multiple stack frames
 * - test with trace that has multiple processes and threads
 * - 'def' task labels and then 'use' as needed
 * - find a way of clipping labels
 * - click and drag on top widget to focus on subset of trace
 * - top widget should visualize the selected region
 * - hook up resize callback and test
 * - top widget scale should cover full data set
 * - show 16ms or 11ms guides in top widget for 60/90fps rendering
 * - implement more system api wrappers
 *      - poll
 *      - select
 *      - epoll_wait
 *      - malloc (only larger allocations)
 *      - pthread apis that might block
 */

var threads = [];
var timestamp_min = 0;
var timestamp_max = 0;

var vbox = document.getElementById('traces-vbox');

/* Urgh, would rather use nicer css flexbox and not
 * fixed pixel sizes :-/
 *
 * using percentages and such is also a bit of a pain
 * since we also need to keep the axis updated.
 */
var left_margin = 200;
var svg_width_px = 1024;
var svg_height_px = 1024;
var svg_trace_width = function () { return svg_width_px - left_margin; };

var levels_per_trace = 5;

function pick_timestamp_formatter(ref)
{
    var base_formatter = d3.format('.3f');
    if (ref > 1)
        return (val) => { return base_formatter(val) + "s"; }
    else if (ref > (1/1000))
        return (val) => { return base_formatter(val * 1000) + "ms"; }
    else if (ref > (1/1000000))
        return (val) => { return base_formatter(val * 1000000) + "µs"; }
    else
        return (val) => { return base_formatter(val * 1000000000) + "ns"; }
}

// For the overview
var widget_x = d3.scaleLinear()
    .domain([0, 1/15])
    .range([0, svg_trace_width()]);

var widget_xaxis = d3.axisBottom()
    .scale(widget_x)
    .tickFormat(pick_timestamp_formatter(1/60))
    //.ticks(1/60)
    ;

// For the detailed traces
var x = d3.scaleLinear()
    .domain([0, 1/15])
    .range([0, svg_trace_width()]);
var y = d3.scaleLinear()
    .domain([0, 10])
    .range([0, 300]);


function string_to_number(str)
{
  var hash = 0, chr, len;

  if (str.length === 0)
      return 0;

  for (var i = 0, len = str.length; i < len; i++) {
    chr   = str.charCodeAt(i);
    hash  = ((hash << 5) - hash) + chr;
    hash |= 0; // Convert to 32bit integer
  }

  return hash / (Math.pow(2,32) - 1);
}

var dummy_task_desc = {
    name: "unknown",
    color: "#ff0000"
};

function get_thread_tasks(thread)
{
    var ancillary = thread.ancillary;
    var tasks = [];

    for (var i = 0; i < ancillary.length; i++) {
        var record = ancillary[i];
        
        switch (record.type) {
            case 'task-desc':
                var desc = {};
                desc.name = record.name;
                var str_num = string_to_number(desc.name);
                /* pick relatively saturated, and light (but not too washed
                 * out for the which background) colors...
                 */
                desc.color = "hsl(" + str_num * 255 + ", " +
                                  (80 + str_num * 20) + "%, " +
                                  (60 + str_num * 20) + "%)";
                tasks[record.index] = desc;
                console.info("Add task description \"" + desc.name + "\"\n");
                break;
        }
    }

    return tasks;
}

function process_task_samples(thread, task_descriptions)
{
    var samples = thread.samples;
    var stack = [];
    var task_samples = [];

    for (var i = 0; i < samples.length; i++) {
        var sample = samples[i];

        /* NB: we have to consider that the dump of data we got might have
         * clipped some number of task push/pop samples so some number of
         * initial samples may be unbalanced
         */
        switch (sample.type) {
            case 1: // task push
                if (stack.length >= (sample.stack_depth + 1)) {
                    console.error("unbalanced task stack push");
                    continue;
                }

                for (var j = stack.length; j < sample.stack_depth; j++)
                    stack.push(null);

                stack.push(sample);
                break;
            case 2: // task pop
                if (stack.length <= sample.stack_depth) {
                    for (var j = stack.length; j <= sample.stack_depth; j++)
                        stack.push(null);
                }

                if (stack[sample.stack_depth] == null) {
                    //XXX: assume task started from time = 0
                    var implicit_sample = JSON.parse(JSON.stringify(sample));
                    implicit_sample.type = 1;
                    implicit_sample.timestamp = 0;

                    stack[sample.stack_depth] = implicit_sample;
                }

                if (stack[sample.stack_depth].task !== sample.task) {
                    console.error("unballanced task stack pop\n");
                    continue;
                }

                var start_sample = stack.pop();

                var task_sample = {
                    cpu_start: start_sample.cpu,
                    cpu_end: sample.cpu,
                    start_time: start_sample.timestamp,
                    end_time: sample.timestamp,
                    task: task_descriptions[sample.task],
                    stack_depth: stack.length

                };

                if (!task_sample.task)
                    task_sample.task = dummy_task_desc;

                //console.log("task: start=" + start_sample.timestamp + ", end=" + sample.timestamp + ", duration=" + (sample.timestamp - start_sample.timestamp));

                task_samples.push(task_sample);

                if (sample.timestamp > timestamp_max)
                    timestamp_max = sample.timestamp;

                /*  XXX: Hack */
                //if (task_samples.length >= 10)
                //    return task_samples;

                break;
        }
    }

    return task_samples;
}

function update_task_traces() {
    /*
var traces_xaxis = d3.axisBottom()
    .scale(x)
    .tickFormat(pick_timestamp_formatter(1/60))
    ;
*/


    //
    // Per-thread SVG
    //

    //UPDATE
    var trace_svgs_update = d3.select('#traces-vbox')
        .selectAll('.thread-trace')
        .data(threads, function (d) { return d.name; });

    //EXIT
    trace_svgs_update.exit().remove();

    //ENTER
    var trace_svgs_new = trace_svgs_update.enter()
        .append('svg')
        .attr("class", "thread-trace")
        //.style("font-family", "Georgia")
        .style("font-size", "15px")
        .style("font-style", "normal")
        .style("font-variant", "normal")
        .style("font-weight", "normal");
        //.style("text-rendering", "optimizeLegibility")
/*
    trace_svgs_new
        .append('g')
        .attr("class", "thread-trace-transform");
*/
    //UPDATE + ENTER
    var trace_svgs_all = trace_svgs_new.merge(trace_svgs_update)
        .attr("width", svg_trace_width())
        .attr("height", svg_height_px);
/*
    var view_domain = widget_x.domain();

    trace_svgs_all
        .select('.thread-trace-transform')
        .attr("transform", "translate(" + x(widget_x.range,0)");
*/

    // 
    // Thread label
    //
    var trace_labels_new = trace_svgs_new
        .append('svg')
        .append('text')
        .attr("class", "thread-name")
        .attr('width', left_margin)
        .attr('height', '1em')
        .attr('x', 0)
        .attr('y', function (d, i) { return y(i * levels_per_trace); })
        .attr('dominant-baseline', 'hanging')
        .text((d) => { 
            return d.name;
        });

    var trace_labels_new = trace_svgs_new
        .append('svg')
        .append('text')
        .attr("class", "thread-name")
        .attr('width', left_margin)
        .attr('height', '1em')
        .attr('x', 0)
        .attr('y', function (d, i) { return y(i * levels_per_trace); })
        .attr('dominant-baseline', 'hanging')
        .text((d) => { 
            return d.name;
        });

    // 
    // Flame graph, task boxes...
    // ===========================

    //UPDATE
    // XXX: it would be good if we had a key for the tasks, so
    // we wouldn't have to reset all their sizes, positions
    // and labels etc (only preserving the DOM elements)
    var task_boxes_update = trace_svgs_all.selectAll('.task')
        .data(function (thread_obj) {

            /* 
             * The traces can get quite unwieldy if we try and
             * represent *everything* in SVG so we filter up
             * front...
             */
            var thread_tasks = thread_obj.task_samples;
            var filtered_tasks = [];
            var current_domain = x.domain();

            for (var i = 0; i < thread_tasks.length; i++) {
                var task = thread_tasks[i];

                if (task.end_time >= current_domain[0] &&
                    task.start_time <= current_domain[1]) {
                    filtered_tasks.push(task);
                }
            }

            return filtered_tasks;
        });

    //EXIT
    task_boxes_update.exit().remove();

    //ENTER
    var task_boxes_new = task_boxes_update.enter()
        .append('svg')
        .attr("class", "task");

    task_boxes_new
        .append('rect');


    // Flame graph task box, background rectangle...
    //

    //UPDATE + ENTER
    var task_boxes_all = task_boxes_new.merge(task_boxes_update);

    // XXX: it would be good if we had a key for the tasks
    task_boxes_all
        .attr('width', function (d) {
            return x(d.end_time) - x(d.start_time);
        })
        .attr('height', function (d) { return y(1) })
        .attr('x', function (d) {
            return left_margin + x(d.start_time);
        })
        .attr('y', function (d) { return y(d.stack_depth); });

    task_boxes_all.select('rect')
        .style('fill', function (d) {
            return d.task.color;
        })
        .attr('width', function (d) {
            return x(d.end_time) - x(d.start_time);
        })
        .attr('height', function (d) { return y(1) });


    
    /*
    trace.append('text')
        .text(function (d) { return d.task.name; })
        .attr('dominant-baseline', 'middle')
        .attr('y', (d) => { return y(1/2); })
        .attr('width', function (d) { return x(d.end_time - d.start_time); })
        .attr('height', function (d) { return y(1) });
        */
    /*
        svg.append("g")
       .attr("class", "x axis")
       .attr("transform", "translate(0," + height + ")")
       .call(xAxis);

       svg.append("g")
       .attr("class", "y axis")
       .call(yAxis)
       .append("text")
       .attr("transform", "rotate(-90)")
       .attr("y", 6)
       .attr("dy", ".71em")
       .style("text-anchor", "end")
       .text("Elevation");
       */
}

var brush = d3.brushX().on("end", brush_ended);

function brush_ended() {
    if (!d3.event.selection)
        x.domain([0, timestamp_max]);
    else
        x.domain(d3.event.selection.map(widget_x.invert));
    update_task_traces();
}

function cut_tasks_before(timestamp) {

    for (var i = 0; i < threads.length; i++) {
        var thread = threads[i];
        var thread_tasks = thread.task_samples;
        var clipped_tasks = [];

        for (var j = 0; j < thread_tasks.length; j++) {
            var task = thread_tasks[j];

            if (task.end_time > timestamp) {
                task.start_time -= timestamp;
                task.end_time -= timestamp;
                clipped_tasks.push(task);
            }
        }

        thread.task_samples = clipped_tasks;
    }
}

d3.json('trace.json', function(data) {

    threads = [];

    var vbox = d3.select('#traces-vbox')
        .html("");

    //widget for selecting the part of the trace to focus on
    var selector_widget = vbox.append('svg')
        .attr("width", svg_trace_width())
        .attr("height", 30)
        .attr("x", left_margin);
        //.style('flex', 1);

    selector_widget
        .append('rect')
        .style('fill', 'lightgrey')
        .attr('width', svg_trace_width())
        .attr('height', function (d) { return y(1) });
    selector_widget.on('click', () => {
        console.log("click");
    });

    selector_widget
        .append("g")
        .attr("class", "brush")
        .call(brush);

    for (var i = 0; i < data.length; i++) {
        var thread = data[i];

        if (thread.type !== "thread")
            continue;

        if (thread.samples.length == 0) {
            console.log("skipping thread with no samples\n");
            continue;
        }

        //x.domain(d3.extent(thread.samples, function(d) { return d.timestamp; }));
        //y.domain([0, d3.max(thread.samples, function(d) { return d.stack_depth; })]);

        thread.task_descriptions = get_thread_tasks(thread);
        thread.task_samples = process_task_samples(thread, thread.task_descriptions);

        if (thread.task_samples.length == 0) {
            console.log("skipping thread with no task samples\n");
            continue;
        }

        threads.push(thread);
        console.log("read samples for process = \"" + thread.name + "\", thread = \"" + thread.thread_name + "\"\n");
    }

    /* We don't want to be juggling too much data, and the UI isn't designed
     * to navigate too large a data set...
     */
    if (timestamp_max > 1.0) {
        cut_tasks_before(timestamp_max - 1.0);
        timestamp_max = 1.0;
    }

    /* FIXME the axis needs to be updated if the window resizes */
    x.domain([0, timestamp_max]);
    widget_x.domain([0, timestamp_max]);
    widget_xaxis.tickFormat(pick_timestamp_formatter(timestamp_max / 2));

    x.range([0, svg_trace_width()]);
    widget_x.range([0, svg_trace_width()]);

    vbox.append('svg')
        //.style('flex', 1)
        .attr("class", "axis")
        .attr("width", svg_trace_width())
        .attr("height", 30)
        .append('g')
        .attr("transform", "translate(0,0)")
        .call(widget_xaxis);

    update_task_traces();
});

/*
vbox.addEventListener('resize', (e) => {
    svg_width_px = e.clientWidth;

    x.range([0, svg_width_px]);
    update_task_traces();
});
*/

    /*
d3.select("#generate")
    .on("click", writeDownloadLink);

function writeDownloadLink(){
    try {
        var isFileSaverSupported = !!new Blob();
    } catch (e) {
        alert("blob not supported");
    }

    var html = d3.select("svg")
        .attr("title", "libut trace")
        .attr("version", 1.1)
        .attr("xmlns", "http://www.w3.org/2000/svg")
        .node().parentNode.innerHTML;

    var blob = new Blob([html], {type: "image/svg+xml"});
    saveAs(blob, "ut.svg");
};
*/
