// This is the source code of the minified APScan core logic
(function () {
	'use strict';

	var AP_PROBE_INTERVAL = 10;     // Check every 10 seconds
	var AP_PROBE_POLL_INTERVAL = 3; // Poll every 3 seconds

	function APScan(url, update_cb) {
		this.url = url;
		this.update_cb = update_cb;
		this.state = {};

		this.probeStart(true);
		this.probeDo();
	}

	APScan.prototype.probeStart = function(poll) {
		if (poll == this.state['poll']) {
			if (this.state['probeTimer']) return;
		} else this.probeStop();
		this.state['poll'] = poll;
		var probeIntv = poll ? AP_PROBE_POLL_INTERVAL : AP_PROBE_INTERVAL;
		var probeTimer = setInterval(this.probeDo.bind(this, false), probeIntv*1000);
		this.state['probeTimer'] = probeTimer;
	};

	APScan.prototype.probeStop = function() {
		if (this.state['probeTimer']) {
			clearInterval(this.state['probeTimer']);
			delete this.state['probeTimer'];
			if (this.state['probe']) {
				this.state['probe'].abort();
			}
		}
	};

	APScan.prototype.probeDo = function(force) {
		if (this.state['probe']) {
			this.update_cb(false, 'Timeout connecting to remote, retrying...');
			this.state['probe'].abort();
		}
		var xhr = new XMLHttpRequest();
		this.state['probe'] = xhr;

		var self = this;
		var queryURL = this.url;
		if (force) queryURL += '?force';
		xhr.open('GET', queryURL, true);
		xhr.onload = function(evt) {
			delete self.state['probe'];
			switch (evt.target.status) {
				case 204:
					self.probeStart(true);
					self.update_cb(false, 'Remote AP scan in progress...');
					break;
				case 200:
					self.probeStart(false);
					self.update_cb(true, evt.target.responseText);
					break;
				default:
					self.probeStart(false);
					self.update_cb(false, 'Unrecognized status ('+evt.target.status+')');
			}
		};
		xhr.onerror = function(evt) {
			delete self.state['probe'];
			if (self.state['probeTimer']) self.probeStart(false);
			self.update_cb(false, 'Error - ' + (evt.error||'Unable to connect to remote'));
		};
		xhr.onabort = function(evt) {
			delete self.state['probe'];
			if (self.state['probeTimer']) self.probeStart(false);
			self.update_cb(false, 'Connection to remote aborted');
		};
		xhr.send();
	};

	APScan.prototype.probeRefresh = function() {
		this.probeDo(true);
	};

	window.APScan = APScan;
})();