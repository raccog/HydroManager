/*const data = [[Date.UTC(2015, 1, 1), 6.21], [Date.UTC(2015, 1, 2), 6.25],
            [Date.UTC(2015, 1, 3), 6.23],
            [Date.UTC(2015, 1, 4), 6.23],
            [Date.UTC(2015, 1, 5), 4.23],
            [Date.UTC(2015, 1, 5), 4.23],
            [Date.UTC(2015, 1, 6), 4.23],
            [Date.UTC(2015, 1, 7), 4.23],
            [Date.UTC(2015, 1, 8), 4.23],
        ];*/
Highcharts.chart('container', {
    chart: {
        zoomType: 'x'
    },
    title: {
        text: 'pH Over Time',
        align: 'left'
    },
    subtitle: {
        text: document.ontouchstart === undefined ?
            'Click and drag in the plot area to zoom in' : 'Pinch the chart to zoom in',
        align: 'left'
    },
    xAxis: {
        type: 'datetime'
    },
    yAxis: {
        title: {
            text: 'pH'
        }
    },
    legend: {
        enabled: false
    },
    plotOptions: {
        series: {
            marker: {
                enabled: true,
                radius: 2.5
            },
        }
    },

    series: [{
        type: 'spline',
        name: 'pH',
        data: ph_data,
        id: 'phSeries',
    },
    {
        type: 'flags',
        name: 'PH EVENTS',
        data: ph_down,
        title: 'PH DOWN',
        onSeries: 'phSeries',
    }]
});
