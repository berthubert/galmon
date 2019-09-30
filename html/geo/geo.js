//
//
//

var fileWorld = 'world.geojson';
var fileAlmanac = '../almanac.json' // 'https://galmon.eu/almanac'
var fileObservers = '../observers.json' // 'https://galmon.eu/observers'

var projectionChoice = 'Gilbert';
var projectionChoice = 'CylindricalStereographic';
var projectionChoice = 'Aitoff';
var projectionChoice = 'Orthographic';
var projectionChoice = 'Fahey';
var projectionChoice = 'Equirectangular';

//
//
//

var svgWorld = d3.select('#svgworld');
var idWorld = document.getElementById('svgworld');

var svgGraticule = d3.select('#svggraticule');
var idGraticule = document.getElementById('svggraticule');

var svgObservers = d3.select('#svgobservers');
var idObservers = document.getElementById('svgobservers');

var svgAlmanac = d3.select('#svgalmanac');
var idAlmanac = document.getElementById('svgalmanac');

var geoPath;
var aProjection;

var speed = 1e-2;
var start = now();

function now()
{
	return Math.round(Date.now() / 1000);
}

function draw_world(data_world)
{
	svgWorld.html("");

	svgWorld.selectAll("path")
		.data(data_world.features)
		.enter()
			.append("path")
				.attr("class", "countries")
				.attr("d", geoPath);
}

function draw_graticule()
{
	var graticule = d3.geoGraticule();

	svgGraticule.html("");

	svgGraticule.selectAll("path")
		.data(graticule.lines())
		.enter()
			.append("path")
				.attr("class", "graticule line")
				.attr("id", function(d) {
					var c = d.coordinates;
					if (c[0][0] == c[1][0]) {
						return (c[0][0] < 0) ? -c[0][0] + "W" : +c[0][0] + "E";
					} else if (c[0][1] == c[1][1]) {
						return (c[0][1] < 0) ? -c[0][1] + "S" : c[0][1] + "N";
					}
				})
				.attr("d", geoPath);

	svgGraticule.selectAll('text')
		.data(graticule.lines())
		.enter()
			.append("text")
				.text(function(d) {
					var c = d.coordinates;
					if ((c[0][0] == c[1][0]) && (c[0][0] % 30 == 0)) {
						return (c[0][0]);
					} else if (c[0][1] == c[1][1]) {
						return (c[0][1]);
					}
				})
				.attr("class", "label")
				.attr("style", function(d) {
					var c = d.coordinates;
					return (c[0][1] == c[1][1]) ? "text-anchor: end" : "text-anchor: middle";
				})
				.attr("dx", function(d) {
					var c = d.coordinates;
					return (c[0][1] == c[1][1]) ? -10 : 0;
				})
				.attr("dy", function(d) {
					var c = d.coordinates;
					return (c[0][1] == c[1][1]) ? 4 : 10;
				})
				.attr('transform', function(d) {
					var c = d.coordinates;
					return ('translate(' + aProjection(c[0]) + ')')
				});

	svgGraticule.append("path")
		.datum(graticule.outline)
		.attr("class", "graticule outline")
		.attr("d", geoPath);
}

function get_almanac_valid(data_almanac)
{
	var a = [];
	Object.keys(data_almanac).forEach(function(e) {
		var o = data_almanac[e];
		o.sv = e;
		if (o["eph-latitude"] != null) {
			o.eph_latitude = o["eph-latitude"];	// json variables with dashes - bad bad
			o.eph_longitude = o["eph-longitude"];
			a.push(o);
		}
	});
	return a;
}

var Tooltip;

function create_tooltop()
{
	// create a tooltip
	Tooltip = d3.select("#combined")
		.append("span")
			.attr("class", "tooltip")
			.attr("id", "tooltip")
			.style("opacity", 0);
}

function to2(num)
{
	return parseFloat(num).toFixed(2);
}

function draw_satellite_to_operator(d)
{
	// get list of observers for this satellite
	satellite = d3.select("#" + d.name);
	a = observers_list_almanac_raw(d);
	for (aa=0;aa<a.length;aa++) {
		observer = d3.select("#Observer_" + a[aa]);

		if (1) {
			svgAlmanac.append("line")
				.attr("class", "radials")
				.attr("x1", Math.round(satellite.attr("cx")))
				.attr("y1", Math.round(satellite.attr("cy")))
				.attr("x2", Math.round(observer.attr("x")) + 4)
				.attr("y2", Math.round(observer.attr("y")) + 4);

		} else {
			svgAlmanac.append("line")
				.attr("class", "radials")
				.attr("d", function(d) {
					return {
							type: "LineString",
							coordinates: [
								[Math.round(satellite.attr("cx")), Math.round(satellite.attr("cy"))],
								[Math.round(observer.attr("x")) + 4, Math.round(observer.attr("y")) + 4]
							]
					};
				});
		}
	}
}

function draw_operator_to_satellite(d)
{
	// get list of satellites for this observer
	observer = d3.select("#Observer_" + d.id);
	a = svs_list_observer_raw(d);
	for (aa=0;aa<a.length;aa++) {
		// check the satellite is seen in almanac data - ie. double check.
		if (data_almanac[a[aa]] && data_almanac[a[aa]].observed) {
			satellite = d3.select("#" + a[aa]);
			svgAlmanac.append("line")
				.attr("class", "radials")
				.attr("x1", Math.round(satellite.attr("cx")))
				.attr("y1", Math.round(satellite.attr("cy")))
				.attr("x2", Math.round(observer.attr("x")) + 4)
				.attr("y2", Math.round(observer.attr("y")) + 4);
		}
	}
}

function draw_almanac(data_almanac)
{
	var arr = get_almanac_valid(data_almanac);

	svgAlmanac.html("");

	// Three function that change the tooltip when user hover / move / leave a cell
	var mouseover = function(d) {
		var o = observers_list_almanac(d);
		s = d.name + " [" + to2(d.eph_longitude) + "," + to2(d.eph_latitude) + "] " + ((o == "") ? "" : " seen by " + o);
		Tooltip.html(s)
			.style("opacity", 1)
			.style("left", (d3.mouse(this)[0]+30) + "px")
			.style("top", (d3.mouse(this)[1]) + "px");
		draw_satellite_to_operator(d);
	}
	var mousemove = function(d) {
		var o = observers_list_almanac(d);
		s = d.name + " [" + to2(d.eph_longitude) + "," + to2(d.eph_latitude) + "] " + ((o == "") ? "" : " seen by " + o);
		Tooltip.html(s)
			.style("left", (d3.mouse(this)[0]+30) + "px")
			.style("top", (d3.mouse(this)[1]) + "px");
	}
	var mouseleave = function(d) {
		Tooltip.html("")
			.style("opacity", 0);
		d3.selectAll(".radials").remove();
	}

	// text first as we want the satellite circles to be always above them!
	svgAlmanac.selectAll("text")
		.data(arr)
		.enter()
		.append("text")
			.attr("class", "labels")
			.text(r => r.sv)
			.attr("dx", r => (aProjection([r.eph_longitude, r.eph_latitude])[0] + 5))
			.attr("dy", r => (aProjection([r.eph_longitude, r.eph_latitude])[1] + 5))
			.attr("fill", r => ((r.observed==true) ? "black" : "#666666"))
			.attr("fill-opacity", r => ((r.observed==true) ? "1.0" : ".5"))
			.attr("stroke-opacity", r => ((r.observed==true) ? "1.0" : ".5"))
			.attr("font-weight", r => ((r.observed==true) ? "bold" : null));

	svgAlmanac.selectAll("circle")
		.data(arr)
		.enter()
		.append("circle")
			.attr("class", "satellites")
			.attr("id", r => r["name"])
			.attr("r", 3)
			.attr("cx", r => aProjection([r.eph_longitude, r.eph_latitude])[0])
			.attr("cy", r => aProjection([r.eph_longitude, r.eph_latitude])[1])
			.attr("fill", function(r) {
				switch (r.gnssid) {
				case 0: return "green";		// GPS
				case 1: return "gray";		// SBAS - not coded
				case 2: return "blue";		// Galileo
				case 3: return "red";		// BeiDou
				case 4: return "gray";		// IMES - not coded
				case 5: return "gray";		// QZSS - not coded
				case 6: return "yellow";	// GLONASS
				default: return "magenta";	// - should not happen
				}
			})
			.attr("fill-opacity", r => ((r.observed==true) ? "1.0" : ".5"))
			.attr("stroke-opacity", r => ((r.observed==true) ? "1.0" : ".5"))
			.on("mouseover", mouseover)
			.on("mousemove", mousemove)
			.on("mouseleave", mouseleave);

}

function age_in_seconds(t)
{
	return now() - t;
}

function a_to_s(a)
{
	var r = "";
	for (aa=0;aa<a.length;aa++) {
		r += a[aa] + " ";
	}
	if (r.length > 0) {
		r = r.slice(0, -1);
	}
	return r;
}

function svs_list_observer_raw(d)
{
	var r = []
	var svs = d.svs;
	for (s in svs) {
		// check the satellite is seen in almanac data - ie. double check.
		if (data_almanac[svs[s].name]) {
			r.push(svs[s].name);
		}
	}
	return r;
}

function observers_list_almanac_raw(d)
{
	var a = [];
	var s_id = d.name;

	for (oo=0;oo<data_observers.length;oo++) {
		o = data_observers[oo];
		o_id = o.id;
		for (s in o.svs) {
			if (s_id == o.svs[s].name) {
				a.push(o_id);
				break;
			}
		}
	}
	return a;
}

function observers_list_almanac(d)
{
	return a_to_s(observers_list_almanac_raw(d));
}

function svs_list_observer(d)
{
	return a_to_s(svs_list_observer_raw(d));
}

function observer_up(d)
{
	var considered_old = 15 * 60;	// 15 mins

	if (age_in_seconds(d["last-seen"]) > considered_old) {
		// data is old
		return false;
	}
	if (Object.keys(d.svs).length == 0) {
		// nothing visible
		return false;
	}
	return true;
}

function draw_observers(data_observers)
{
	// Three function that change the tooltip when user hover / move / leave a cell
	var mouseover = function(d) {
		var o = svs_list_observer(d);
		s = d.id + ": [" + to2(d.longitude) + "," + to2(d.latitude) + "]" + ((o == "") ? "" : " sees " + svs_list_observer(d));
		Tooltip.html(s)
			.style("opacity", 1)
			.style("left", (d3.mouse(this)[0]+30) + "px")
			.style("top", (d3.mouse(this)[1]) + "px");
		draw_operator_to_satellite(d);
	}
	var mousemove = function(d) {
		var o = svs_list_observer(d);
		s = d.id + ": [" + to2(d.longitude) + "," + to2(d.latitude) + "]" + ((o == "") ? "" : " sees " + svs_list_observer(d));
		Tooltip.html(s)
			.style("left", (d3.mouse(this)[0]+30) + "px")
			.style("top", (d3.mouse(this)[1]) + "px");
	}
	var mouseleave = function(d) {
		Tooltip.html("")
			.style("opacity", 0);
		d3.selectAll(".radials").remove();
	}

	if (0) {
		// we draw a geo correct rectangle
		var observer_degrees = 3.0;
		svgAlmanac.selectAll("div")
			.data(data_observers)
			.enter()
				.append("path")
				.attr("class", r => (observer_up(r) ? "observers" : "observers down"))
				.attr("id", r => ("Observer_" + r["id"]))
				.attr("d", function(d) {
					return geoPath({
							type: "LineString",
							coordinates: [
								[d.longitude - observer_degrees/2, d.latitude - observer_degrees/2],
								[d.longitude - observer_degrees/2, d.latitude + observer_degrees/2],
								[d.longitude + observer_degrees/2, d.latitude + observer_degrees/2],
								[d.longitude + observer_degrees/2, d.latitude - observer_degrees/2],
							]
						});
				})
				.on("mouseover", mouseover)
				.on("mousemove", mousemove)
				.on("mouseleave", mouseleave);
	} else {
		rect_size = 8;
		svgAlmanac.selectAll("div")
			.data(data_observers)
			.enter()
				.append("rect")
				.attr("class", r => (observer_up(r) ? "observers" : "observers down"))
				.attr("id", r => ("Observer_" + r["id"]))
				.attr("width", rect_size)
				.attr("height", rect_size)
				.attr("x", r => (aProjection([r.longitude, r.latitude])[0] - rect_size/2))
				.attr("y", r => (aProjection([r.longitude, r.latitude])[1] - rect_size/2))
				.on("mouseover", mouseover)
				.on("mousemove", mousemove)
				.on("mouseleave", mouseleave);
	}

	// kick off the annimation - if needed
	annimate_down_observers();
}

var down_timer;

function annimate_down_observers()
{
	clearTimeout(down_timer);

	var down = document.getElementsByClassName('observers down');
	if (down.length > 0) {
		// if we have an observer that is down - lets annoutate it!
		for (var ii=0, ll=down.length; ii<ll; ii++){
			down[ii].style.fill = (down[ii].style.fill == "black") ? "red" : "black";
		}
	}
	var seconds = 1;
	down_timer = setTimeout(annimate_down_observers, seconds * 1000.0);
}

function draw_observers_coverage(data_observers)
{
	var radius = 65;			// XXX fix
	var geoCircle = d3.geoCircle();

	svgObservers.html("");

	// we draw a geo correct circle
	svgObservers.selectAll("div")
		.data(data_observers)
		.enter()
			.append("path")
			.attr("class", r => (observer_up(r) ? "coverage" : "coverage down"))
			.attr("d", function(r) {
				return geoPath(geoCircle.center([r["longitude"], r["latitude"]]).radius(radius)());
			});
}

var data_almanac = null;
var data_observers = null;

function do_update_almanac_observers(error, results)
{
	var updated_almanac = false;
	var updated_observers = false;

	if (results.length > 0 && results[0]) {
		data_almanac = results[0];
		updated_almanac = true;
	}
	if (results.length > 1 && results[1]) {
		data_observers = results[1];
		updated_observers = true;
	}

	// XXX cheat for now - we need until we fix d3/svg bug above
	updated_almanac = true;
	updated_observers = true;

	if (updated_almanac) {
		// We write into the svgalmanac area - so clean it and rewrite it
		draw_almanac(data_almanac);
	}
	if (updated_observers) {
		draw_observers_coverage(data_observers)
		draw_observers(data_observers);
	}
}

var repeat_timer;
var display_observers_count = 0;

function do_timer()
{
	clearTimeout(repeat_timer);

	if (display_observers_count == 0) {
		// observers does not need that much updating!
		d3.queue(1)
			.defer(d3.json, fileAlmanac + '?t=' + now())
			.defer(d3.json, fileObservers + '?t=' + now())
			.awaitAll(do_update_almanac_observers);
		display_observers_count = 10;
	} else {
		// just queue an update to almanac
		d3.queue(1)
			.defer(d3.json, fileAlmanac + '?t=' + now())
			.awaitAll(do_update_almanac_observers);
	}
	display_observers_count--;

	var seconds = 60;
	repeat_timer = setTimeout(do_timer, seconds * 1000.0);
}

function set_projection(data_world)
{
	// var aProjection = d3.geoMercator().scale(100).translate([250, 250]);
	// all this complexity is so we can scale to full screen.
	// see: https://stackoverflow.com/questions/14492284/center-a-map-in-d3-given-a-geojson-object

	var center = [0, 0];		// This is very Euro-centric - but that's how these projections works.
	var scale = 191;		// No idea what this does

	var svgCombined = d3.select('#combined');
	var idCombined = document.getElementById("combined");

	svgWorld = d3.select("#svgworld");
	idWorld = document.getElementById("svgworld");

	svgGraticule = d3.select("#svggraticule");
	idGraticule = document.getElementById("svggraticule");

	var offset = [idCombined.clientWidth/2, idCombined.clientHeight/2];

	switch(projectionChoice) {
	default:
		console.log(projectionChoice + ': not coded');
		// fall thru to Equirectangular
	case 'Equirectangular':
		aProjection = d3.geoEquirectangular()
				.scale(scale)
				.translate(offset);
		break;
	case 'Aitoff':
		aProjection = d3.geoAitoff()
				.scale(scale)
				.translate(offset);
		break;
	case 'CylindricalStereographic':
		aProjection = d3.geoCylindricalStereographic()
				.scale(scale)
				.translate(offset);
		break;
	case 'Fahey':
		aProjection = d3.geoFahey()
				.scale(scale)
				.translate(offset);
		break;
	case 'Gilbert':
		aProjection = d3.geoGilbert()
				.scale(scale)
				.translate(offset);
		break;
	case 'Orthographic':
		aProjection = d3.geoOrthographic()
				.scale(scale)
				.translate(offset);
		break;
	}

	geoPath = d3.geoPath()
			.projection(aProjection);

	// using the path determine the bounds of the current map and use
	// these to determine better values for the scale and translation
	var bounds = geoPath.bounds(data_world);
	var hscale = scale * (idCombined.clientWidth - 40) / (bounds[1][0] - bounds[0][0]);
	var vscale = scale * (idCombined.clientHeight - 40) / (bounds[1][1] - bounds[0][1]);
	scale = (hscale < vscale) ? hscale : vscale;
	var offset = [
			idCombined.clientWidth - (bounds[0][0] + bounds[1][0])/2,
			idCombined.clientHeight - (bounds[0][1] + bounds[1][1])/2
			];

	if (0) {
		// new projection
		switch(projectionChoice) {
		default:
			console.log(projectionChoice + ': not coded');
			// fall thru to Equirectangular
		case 'Equirectangular':
			aProjection = d3.geoEquirectangular()
					.center(center)
					.scale(scale)
					.translate(offset);
			break;
		case 'Aitoff':
			aProjection = d3.geoAitoff()
					.center(center) .scale(scale)
					.translate(offset);
			break;
		case 'CylindricalStereographic':
			aProjection = d3.geoCylindricalStereographic()
					.center(center)
					.scale(scale)
					.translate(offset);
			break;
		case 'Fahey':
			aProjection = d3.geoFahey()
					.center(center)
					.scale(scale)
					.translate(offset);
			break;
		case 'Gilbert':
			aProjection = d3.geoGilbert()
					.center(center)
					.scale(scale)
					.translate(offset);
			break;
		case 'Orthographic':
			aProjection = d3.geoOrthographic()
					.center(center)
					.scale(scale)
					.translate(offset);
			break;
		}
	}

	svgCombined.attr("width", idCombined.clientWidth);
	svgCombined.attr("height", idCombined.clientHeight);

	padding = 0 // 20 - see svg: padding in css

	svgWorld.attr("width", idCombined.clientWidth - padding * 2);
	svgWorld.attr("height", idCombined.clientHeight - padding * 2);

	svgObservers.attr("width", idCombined.clientWidth - padding * 2);
	svgObservers.attr("height", idCombined.clientHeight - padding * 2);

	svgGraticule.attr("width", idCombined.clientWidth - padding * 2);
	svgGraticule.attr("height", idCombined.clientHeight - padding * 2);

	svgAlmanac.attr("width", idCombined.clientWidth - padding * 2);
	svgAlmanac.attr("height", idCombined.clientHeight - padding * 2);

}

function do_draw_world(data_world)
{
	set_projection(data_world);

	create_tooltop();

	draw_world(data_world);
	draw_graticule();

	if (projectionChoice == 'Orthographic') {
		// rotating globe is done with a constant timer
		d3.timer(function() {
			var lambda = speed * (Date.now() - start);	// λ
			var phi = -15;					// φ
			aProjection.rotate([lambda + 180, -phi]);

			draw_world(data_world);
			draw_graticule();
			do_update_almanac_observers(null, [])
		});
	}

}

function read_world()
{
	d3.json(fileWorld, function(result) {
		var data_world = result;
		do_draw_world(data_world);
		// after the world is read in and displayed - then start the timers!
		// we don't redraw the world!
		do_timer();
	});
}

function geo_start()
{
	read_world();
}

geo_start();

// d3.select("body").onresize = do_timer;

