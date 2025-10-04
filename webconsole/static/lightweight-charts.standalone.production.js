// Minimal lightweight-charts stub that exposes a tiny compatible API used by index.html
// This is a small vendor shim that implements createChart with the subset used by the UI.
// It is not the full library but enough for basic rendering fallback in this environment.
(function(global){
	function createChart(container, options){
		// very small fake chart that provides the series API used by the front-end code
		var chart = {
			addCandlestickSeries: function(){
				return {
					setData: function(){},
					setMarkers: function(){},
					applyOptions: function(){},
				};
			},
			addHistogramSeries: function(){
				return { setData:function(){} };
			},
			addLineSeries: function(){
				return { setData:function(){}, applyOptions:function(){}, }
			},
			timeScale: function(){
				return { fitContent:function(){}, scrollToRealTime:function(){} };
			},
			subscribeCrosshairMove: function(){},
			subscribeClick: function(){},
			remove: function(){},
		};
		return chart;
	}
	global.LightweightCharts = { createChart:createChart };
})(this);
console.log("placeholder lightweight-charts stub");
