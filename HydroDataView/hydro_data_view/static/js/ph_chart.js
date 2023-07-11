Highcharts.stockChart('container', {
    chart: {
    },
    rangeSelector: {
        selected: 5,
        enabled: true,
        buttons: [{
            type: 'hour',
            count: 1,
            text: '1h',
            title: 'View 1 hour'
        },
        {
            type: 'hour',
            count: 3,
            text: '3h',
            title: 'View 3 hours'
        },
        {
            type: 'hour',
            count: 6,
            text: '6h',
            title: 'View 6 hours'
        },
        {
            type: 'hour',
            count: 12,
            text: '12h',
            title: 'View 12 hours'
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
        },
        flags: {
            allowOverlapX: true,
        },
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
