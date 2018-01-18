"use strict";

//# sourceURL=ut.js

/*
 * TODO:
 *
 * - insert axis for details
 * - align process/thread names with trace
 * - test with trace that has multiple stack frames
 * - test with trace that has multiple processes and threads
 * - 'def' task labels and then 'use' as needed
 * - hook up resize callback and test
 * - show 16ms or 11ms guides in top widget for 60/90fps rendering
 * - implement more system api wrappers
 *      - malloc (only larger allocations)
 * - cull tiny/brief tasks
 */

var threads = [];
var timestamp_min = 0;
var timestamp_max = 0;

var process_name_local = d3.local();
var brush_local = d3.local();

var target_frame_duration = 1/90;

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

var levels_per_trace = 2;

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
    .range([0, 200]);


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
                    stack_depth: stack.length - 1,
                };

                if (!task_sample.task)
                    task_sample.task = dummy_task_desc;

                //console.log("task: start=" + start_sample.timestamp + ", end=" + sample.timestamp + ", duration=" + (sample.timestamp - start_sample.timestamp));

                task_samples.push(task_sample);

                if (sample.timestamp > thread.timestamp_max)
                    thread.timestamp_max = sample.timestamp;

                if (start_sample.timestamp < thread.timestamp_min)
                    thread.timestamp_min = start_sample.timestamp;

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
        .data(threads, function (d) { return d.thread_name; });

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
        .attr("height", y(levels_per_trace))
        .attr('x', 0)
        .attr('y', function (d, i) { return y(i * levels_per_trace); });
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
        .attr('dominant-baseline', 'hanging')
        .text((d) => { 
            return d.thread_name;
        });
/*
    var trace_labels_new = trace_svgs_new
        .append('svg')
        .append('text')
        .attr("class", "thread-name")
        .attr('width', left_margin)
        .attr('height', '1em')
        .attr('dominant-baseline', 'hanging')
        .text((d) => { 
            return d.thread_name;
        });
*/
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

            console.log("filtered " + thread_obj.task_samples.length +  " tasks down to " + filtered_tasks.length + " tasks for thread = " + thread_obj.thread_name);

            return filtered_tasks;
        },
        function (d) { return d.start_time; } //key
        );

    //EXIT
    task_boxes_update.exit().remove();

    //ENTER
    var task_boxes_new = task_boxes_update.enter()
        .append('svg')
        .attr("class", "task");

    task_boxes_new
        .append('rect');

    task_boxes_new
        .append('text')
        .attr('dominant-baseline', 'hanging');

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

    task_boxes_all.select('text')
        .text(function (d) {
            var width = x(d.end_time) - x(d.start_time);
            if (width > 50)
                return d.task.name;
            else
                return "...";
        });

    
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

    console.log("clipping anything before " + timestamp);
    for (var i = 0; i < threads.length; i++) {
        var thread = threads[i];
        var thread_tasks = thread.task_samples;
        var clipped_tasks = [];

        console.log("clipping thread with timestamp range: " + thread.timestamp_min + " to " + thread.timestamp_max);

        for (var j = 0; j < thread_tasks.length; j++) {
            var task = thread_tasks[j];

            if (task.end_time > timestamp) {
                task.start_time -= timestamp;
                task.end_time -= timestamp;
                clipped_tasks.push(task);
            }
        }

        console.log("clipped " + thread.task_samples.length + " tasks down to " + clipped_tasks.length + " tasks for thread = " + thread.thread_name);
        thread.task_samples = clipped_tasks;
    }
}


/*
 * Throttle brush zooming to ~10fps otherwise it'll easily hang/DOS your
 * machine.
 */
var brush_update_queued = false;
var brush_queued_move = null;

function queue_brush_update() {
    if (brush_update_queued)
        return;

    brush_update_queued = true;
    window.setTimeout(function () {

        if (brush_queued_move !== null) {
            var fn = brush_queued_move;
            brush_queued_move = null;
            fn();
        }

        // clear afterwards, so if handling the
        // brush update is very slow then we don't
        // want to pile up updates faster than
        // we can handle them...
        brush_update_queued = false;
    }, 100);
}

d3.json('trace.json', function(data) {

    threads = [];

    var vbox = d3.select('#traces-vbox')
        .html("");

    for (var i = 0; i < data.length; i++) {
        var thread = data[i];

        if (thread.type !== "thread")
            continue;

        if (thread.samples.length == 0) {
            console.log("skipping thread with no samples\n");
            continue;
        }

        if (thread.thread_name !== "hellovr"
            && thread.thread_name !== "LighthouseDirec")
            continue;

        thread.timestamp_min = Number.MAX_VALUE;
        thread.timestamp_max = 0;

        //x.domain(d3.extent(thread.samples, function(d) { return d.timestamp; }));
        //y.domain([0, d3.max(thread.samples, function(d) { return d.stack_depth; })]);

        thread.task_descriptions = get_thread_tasks(thread);
        thread.task_samples = process_task_samples(thread, thread.task_descriptions);

        if (thread.task_samples.length == 0) {
            console.log("skipping thread with no task samples\n");
            continue;
        }

        if (thread.timestamp_max > timestamp_max)
            timestamp_max = thread.timestamp_max;

        threads.push(thread);
        console.log("read samples for process = \"" + thread.name + "\", thread = \"" + thread.thread_name + "\"\n");
        console.log("> timestamp range is " + thread.timestamp_min + " to " + thread.timestamp_max);
    }

    /* We don't want to be juggling too much data, and the UI isn't designed
     * to navigate too large a data set...
     */
    //if (timestamp_max > 0.5) {
    //    cut_tasks_before(timestamp_max - 0.5);
    //    timestamp_max = 0.5;
   // }

    /* FIXME the axis needs to be updated if the window resizes */
    x.domain([0, timestamp_max]);
    widget_x.domain([0, timestamp_max]);
    widget_xaxis.tickFormat(pick_timestamp_formatter(timestamp_max / 2));

    x.range([0, svg_trace_width()]);
    widget_x.range([0, svg_trace_width()]);


    //widget for selecting the part of the trace to focus on
    var selector_widget = vbox.append('svg')
        .attr("width", svg_trace_width())
        .attr("height", 30)
        .attr("x", left_margin);
        //.style('flex', 1);

    //
    // Bars delimiting frames in top selector widget...
    //
    
    //UPDATE
    var selector_bars_update = selector_widget.selectAll('.frame-bar')
        .data(
            d3.range(widget_x.domain()[0],
                     widget_x.domain()[1],
                     target_frame_duration)
        );

    //EXIT
    selector_bars_update.exit().remove();

    //ENTER
    var selector_bars_new = selector_bars_update.enter()
        .append('rect')
        .attr('class', 'frame-bar')
        .attr('width', widget_x(target_frame_duration))
        .attr('height', 30)
        .attr('x', function (d) {
            return widget_x(d);
        })
        .style('fill', function (d, i) {
            return i%2 ? 'lightgrey': 'darkgrey';
        });


    //UPDATE + ENTER
    //var selector_bars_all = selector_bars_new.merge(selector_bars_update)


    var selector_brush_grp = selector_widget
        .append("g")
        .attr("class", "brush")

    var selector_brush = selector_brush_grp
        .call(brush);
        //.select('g');

    selector_widget//.select('.overlay')
        //.each(function () {
        //    console.log("WTF\n");            
        //    brush_local.set(this, brush);
        //})
        .on('wheel', function (d) {

            var e = d3.event;

            //Grrr, getting back to a brush so that d3.brushSelection() can be
            //used is a F'ing pain - just punching through and accessing
            //the internal _groups array - this is just horrible though.
            var sel = d3.brushSelection(selector_brush._groups[0][0]);

            if (sel === null) {
                console.log("scroll with no brush selection");
                return;
            }
            var current_domain = x.domain();
            
            var dir = e.deltaY > 0 ? "down": "up";

            var range = sel[1] - sel[0];
            var half_range = range / 2;

            var x0 = sel[0];
            var x1 = sel[1];

            if (e.clientX > sel[0] && e.clientX < sel[1]) {
                console.log("scroll " + dir + " in brush");
                var mid = sel[0] + half_range;
                var factor = e.shiftKey ? 0.99 : 0.9;
                var new_half_range = half_range * Math.pow(factor, e.deltaY);

                x0 = mid - new_half_range;
                x1 = mid + new_half_range;
            } else if (e.clientX < sel[0]) {
                console.log("scroll " + dir + " left of brush");
                var factor = e.shiftKey ? 0.01 : 0.1;
                var dx = half_range * factor * e.deltaY;
                x0 += dx;
                x1 += dx;
            } else {
                console.log("scroll " + dir + " right of brush");
                var factor = e.shiftKey ? 0.01 : 0.1;
                var dx = half_range * factor * e.deltaY;
                x0 += dx;
                x1 += dx;
            }

            var x0 = Math.max(widget_x.range()[0], x0);
            var x1 = Math.min(widget_x.range()[1], x1);

            brush_queued_move = function () {
                console.log("throttled brush move\n");
                brush.move(selector_brush, [x0, x1]);
            };

            queue_brush_update();
            console.log("wheel");
            d3.event.preventDefault();
        }, false /* capture */);


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
