Highcharts.stockChart('container', {
    chart: {
    },
    rangeSelector: {
        selected: 2,
        enabled: true,
        buttons: [{
            type: 'hour',
            count: 1,
            text: '1h',
            title: 'View 1 hour'
        },
        {
            type: 'day',
            count: 1,
            text: '1d',
            title: 'View 1 day'
        },
        {
            type: 'all',
            text: 'All',
            title: 'View all'
        }]
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
        name: 'pH Down Pump Pulses',
        data: ph_down,
        title: 'pH Down',
        onSeries: 'phSeries',
    },
    {
        type: 'flags',
        name: 'pH Up Pump Events',
        data: ph_up,
        title: 'pH Up',
        onSeries: 'phSeries',
    }
    ]
});
