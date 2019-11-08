//
//
//

var fileWorld = "world.geojson";
var fileAlmanac = "../almanac.json" // "https://galmon.eu/almanac"
var fileObservers = "../observers.json" // "https://galmon.eu/observers"

var projectionChoice = "Fahey";
var projectionChoice = "CylindricalStereographic";
var projectionChoice = "Equirectangular";
//var projectionChoice = "Aitoff";

//
//
//

var svgWorld = d3.select("#svgworld");
var idWorld = document.getElementById("svgworld");

var svgGraticule = d3.select("#svggraticule");
var idGraticule = document.getElementById("svggraticule");

var svgObservers = d3.select("#svgobservers");
var idObservers = document.getElementById("svgobservers");

var svgAlmanac = d3.select("#svgalmanac");
var idAlmanac = document.getElementById("svgalmanac");

var geoPath;
var aProjection;

function draw_world(data_world)
{
	// console.log("draw_world() " + data_world.features.length);
	
	svgWorld.selectAll("path")
		.data(data_world.features)
		.enter()
		.append("path")
			.attr("class", "countries")
			.attr("d", geoPath);
}

function draw_graticule()
{
	// Graticule
	var graticule = d3.geoGraticule();

	// console.log("draw_graticule()");

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
			.attr("class","label")
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
	var a=[];
	Object.keys(data_almanac).forEach(function(e) {
		var o = data_almanac[e];
		o.sv = e;
		if (o["eph-latitude"] != null) {
			a.push(o);
		}
	});
	return a;
}

function draw_almanac(data_almanac)
{
	var arr = get_almanac_valid(data_almanac);

	display_gnssid = {
		0: d3.select("#GPS").property("checked"),
		2: d3.select("#Gal").property("checked"),
		3: d3.select("#BeiDou").property("checked"),
		6: d3.select("#Glonass").property("checked")
	};

	arr = arr.filter(sat => display_gnssid[sat.gnssid]);

	console.log("draw_almanac() " + arr.length);

	svgAlmanac.selectAll("circle")
		.data(arr)
		.enter()
		.append("circle")
			.attr("class", "sats")
			.attr("r", 3)
			.attr("cx", d => aProjection([d["eph-longitude"],d["eph-latitude"]])[0])
			.attr("cy", d => aProjection([d["eph-longitude"],d["eph-latitude"]])[1])
			.attr("fill", function(d) {
				switch (d.gnssid) {
				case 0: return "green";		// GPS
				case 1: return "gray";		// SBAS	 - not coded
				case 2: return "blue";		// Galileo
				case 3: return "red";		// BeiDou
				case 4: return "gray";		// IMES	 - not coded
				case 5: return "gray";		// QZSS	 - not coded
				case 6: return "yellow";	// GLONASS
				default: return "magenta";	// - should not happen
				}
			});

	svgAlmanac.selectAll("text")
		.data(arr)
		.enter()
		.append("text")
			.attr("class", "labels")
			.text(d => d.sv)
			.attr("dx", d => 5+aProjection([d["eph-longitude"],d["eph-latitude"]])[0])
			.attr("dy", d => 5+aProjection([d["eph-longitude"],d["eph-latitude"]])[1])
			.attr("fill", function(d) {
				if (d.observed==true)
					return "black";
				return "#666666";
			})
			.attr("font-weight", function(d) {
				if (d.observed==true)
					return "bold";
				return null;
			});
}

function draw_observers(data_observers)
{
	console.log("draw_observers() " + data_observers.length);

	svgObservers.selectAll("rect")
		.data(data_observers)
		.enter()
			.append("rect")
			.attr("class", "sats")
			.attr("width", 8)
			.attr("height", 8)
			.attr("x", d => aProjection([d["longitude"],d["latitude"]])[0]-4)
			.attr("y", d => aProjection([d["longitude"],d["latitude"]])[1]-4)
			.attr("fill", function(d) { return "black"; });

}

function draw_observers_coverage(data_observers)
{
	var radius = 55;			// XXX fix
	var geoCircle = d3.geoCircle();
	svgObservers.selectAll("path")
		.data(data_observers)
		.enter()
			.append("path")
			.attr("class", "coverage")
			.attr("d", function(r) {
				// console.log([r["longitude"], r["latitude"]] + " = " + aProjection([r["longitude"], r["latitude"]]));
				return geoPath(geoCircle.center([r["longitude"],r["latitude"]]).radius(radius)());
			});

}

var display_observers_count = 0;

function do_update_almanac(error, results)
{
	var data_almanac = results[0];
	var data_observers = results[1];

	// console.log("do_update_almanac() " + Object.keys(data_almanac).length + " " + data_observers.length);

	// Zero out the observers if they're not meant to display
	if (!d3.select("#Obs").property("checked")){
		svgObservers.html("");
	}

	if (display_observers_count == 0 || d3.select("#Obs").property("checked")) {
		// does not need that much updating!
		svgObservers.html("");
		draw_observers(data_observers);
		draw_observers_coverage(data_observers)
		display_observers_count = 10;
	}
	display_observers_count--;

	// We write into the svgalmanac area - so clean it and rewrite it
	svgAlmanac.html("");
	draw_almanac(data_almanac);
}

var repeat;

function do_timer()
{
	var seconds = 60;
	clearTimeout(repeat);
	repeat = setTimeout(do_timer, 1000.0*seconds);

	d3.queue(1)
		.defer(d3.json, fileAlmanac)
		.defer(d3.json, fileObservers)
		.awaitAll(do_update_almanac);
}

function set_projection(data_world)
{
	// var aProjection = d3.geoMercator().scale(100).translate([250,250]);
	// all this complexity is so we can scale to full screen.
	// see: https://stackoverflow.com/questions/14492284/center-a-map-in-d3-given-a-geojson-object

	var center = [0,0];		// This is very Euro-centric - but that's how these projections works.
	var scale  = 210;		// No idea what this does

	var idCombined = document.getElementById("combined");

	svgWorld = d3.select("#svgworld");
	idWorld = document.getElementById("svgworld");

	svgGraticule = d3.select("#svggraticule");
	idGraticule = document.getElementById("svggraticule");

	switch(projectionChoice) {
	default:
		console.log(projectionChoice + ": not coded");
		// fall thru to Equirectangular
	case 'Equirectangular':
		aProjection = d3.geoEquirectangular()
				.scale(scale)
				.translate([idCombined.clientWidth/2,idCombined.clientHeight/2]);
		break;
	case 'Aitoff':
		aProjection = d3.geoAitoff()
				.scale(scale)
				.translate([idCombined.clientWidth/2,idCombined.clientHeight/2]);
		break;
	case 'CylindricalStereographic':
		aProjection = d3.geoCylindricalStereographic()
				.scale(scale)
				.translate([idCombined.clientWidth/2,idCombined.clientHeight/2]);
		break;
	case 'Fahey':
		aProjection = d3.geoFahey()
				.scale(scale)
				.translate([idCombined.clientWidth/2,idCombined.clientHeight/2]);
		break;
	case 'Gilbert':
		aProjection = d3.geoGilbert()
				.scale(scale)
				.translate([idCombined.clientWidth/2,idCombined.clientHeight/2]);
		break;
	}

	geoPath = d3.geoPath()
			.projection(aProjection);

	// using the path determine the bounds of the current map and use
	// these to determine better values for the scale and translation
	var bounds  = geoPath.bounds(data_world);;
	var hscale  = scale*idCombined.clientWidth  / (bounds[1][0] - bounds[0][0]);
	var vscale  = scale*idCombined.clientHeight / (bounds[1][1] - bounds[0][1]);
	scale   = (hscale < vscale) ? hscale : vscale;
	var offset  = [
			idCombined.clientWidth - (bounds[0][0] + bounds[1][0])/2,
			idCombined.clientHeight - (bounds[0][1] + bounds[1][1])/2
			];

	if (0) {

	// new projection
	switch(projectionChoice) {
	default:
		console.log(projectionChoice + ": not coded");
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
	}

	}

	console.log("do_draw_world() " + "width=" + idCombined.clientWidth + "," + "height=" + idCombined.clientHeight);

	svgWorld.attr("width", idCombined.clientWidth);
	svgWorld.attr("height", idCombined.clientHeight);

	svgObservers.attr("width", idCombined.clientWidth);
	svgObservers.attr("height", idCombined.clientHeight);

	svgGraticule.attr("height", idCombined.clientHeight);
	svgGraticule.attr("width", idCombined.clientWidth);

	svgAlmanac.attr("height", idCombined.clientHeight);
	svgAlmanac.attr("width", idCombined.clientWidth);

}

function do_draw_world(data_world)
{
	// console.log("do_draw_world()");

	set_projection(data_world);

	svgWorld.html("");
	draw_world(data_world);

	svgGraticule.html("");
	draw_graticule();
}

function read_world()
{
	// console.log("read_world()");
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
	// console.log("geo_start()");
	read_world();
}

geo_start();

// d3.select("body").onresize = do_timer;

