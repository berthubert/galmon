var repeat;

function maketable(str, arr)
{
    var table=d3.select(str);
    table.html("");
    var thead=table.append("thead");
    var tbody=table.append("tbody");
    
    var rows=tbody.selectAll("tr").
        data(arr).
        enter().
        append("tr");
    
    var columns = ["sv", "iod", "eph-age-m", "sisa", "e1bhs", "e1bdvs", "e5bhs", "e5bdvs", "a0", "a1", "elev", "calc-elev", "db", "last-seen-s"];    
    
    // append the header row
    thead.append("tr")
        .selectAll("th")
        .data(columns)
        .enter()
        .append("th")
        .text(function(column) { return column; });
    
    var cells = rows.selectAll("td").
        data(function(row) {
            return columns.map(function(column) {
                var ret={};
                ret.column = column;
                ret.color="white";
                if(column == "eph-age-m") {
                    var b = moment.duration(row[column], 'm');
                    ret.value = b.humanize();
                }
                else if(column == "last-seen-s") {
                    var b = moment.duration(row[column], 's');
                    ret.value = b.humanize();
                }
                else if(column == "calc-elev") 
                    ret.value = row[column].toFixed(1);
                else if(column == "e1bdvs" || column =="e5bdvs")  {                    
                    if(row[column] == 0)
                        ret.value = "valid";
                    else if(row[column] == 1)
                        ret.value = "working no guarantee"                
                }
                else if(column == "e1bhs" || column =="e5bhs")  {
                    if(row[column] == 0)
                        ret.value = "ok";
                    else if(row[column] == 1)
                        ret.value = "out of service"
                    else if(row[column] == 2)
                        ret.value = "will be out of service"
                    else if(row[column] == 3) {
                        ret.color="orange";
                        ret.value = "test"
                    }
                }                
                else {
                    ret.value= row[column];
                }
                if(column == "eph-age-m" && row[column] > 30 && row["last-seen-s"] < 120)
                    ret.color="orange";
                if(column == "sisa" && parseInt(row[column]) > 312)
                    ret.color="#ffaaaa";
                if(column == "sisa" && row[column]=="NO SIS AVAILABLE")
                    ret.color="#ff2222";                    
                return ret;
            })
        })
        .enter().append("td").text(function(d) { return d.value; }).attr("align", "right").style("background-color", function(d) {
            return d.color;
        });

}

var sats={};

function update()
{
    var seconds = 2;
    clearTimeout(repeat);
    repeat=setTimeout(update, 1000.0*seconds);

    d3.json("svs", function(d) {
        // put data in an array
        sats=d;
        var arr=[];
        Object.keys(d).forEach(function(e) {
            var o = d[e];
            o.sv=e;
            arr.push(o);
        });

        // sort it on SV
        arr.sort(function(a, b) {
            if(Number(a.sv) < Number(b.sv))
                return -1;
            if(Number(b.sv) < Number(a.sv))
                return 1;
            return 0;
        });

        var livearr=[], stalearr=[];
        for(n = 0 ; n < arr.length; n++)
        {
            if(arr[n]["last-seen-s"] < 120)
                livearr.push(arr[n]);
            else
                stalearr.push(arr[n]);
        }

        maketable("#svs", livearr);
        maketable("#svsstale", stalearr);
        
    });
}

repeat=update();


