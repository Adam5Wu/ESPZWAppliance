// This is the source code of the minified OTA core logic
(function () {
	'use strict';

	var OTA_PROBE_INTERVAL = 10;

	function OTA(url, avail_cb) {
		this.url = url;
		this.avail_cb = avail_cb;
		this.state = {};
		this.state['avail'] = false;
		this.state['avail_comp'] = {};

		this.selectFile();
		this.probeStart();
	}

	OTA.prototype.probeStart = function() {
		if (this.state['probeTimer']) return;
		var probeTimer = setInterval(this.probeDo.bind(this), OTA_PROBE_INTERVAL*1000);
		this.state['probeTimer'] = probeTimer;
		this.probeDo();
	};

	OTA.prototype.probeStop = function() {
		if (this.state['probeTimer']) {
			clearInterval(this.state['probeTimer']);
			delete this.state['probeTimer'];
			if (this.state['probe']) {
				this.state['probe'].abort();
			}
		}
	};

	OTA.prototype.setAvail = function(comp, state, reason) {
		this.state.avail_comp[comp] = {
			'name': comp,
			'state': state,
			'reason': reason
		};
		var ucomp;
		for (var name in this.state.avail_comp) {
			if (!this.state.avail_comp[name].state) {
				ucomp = this.state.avail_comp[name];
				break;
			}
		}
		if (!!ucomp) {
			this.state.avail = false;
			this.avail_cb(ucomp.state, ucomp.name, ucomp.reason);
		} else if (!this.state.avail && !ucomp) {
			this.state.avail = true;
			this.avail_cb(true);
		}
	};

	OTA.prototype.getAvailComp = function(name) {
		if (name === undefined)
			return this.state.avail_comp;
		return this.state.avail_comp[name];
	};

	OTA.prototype.probeDo = function() {
		if (this.state['probe']) {
			this.setAvail('remote', false, 'Timeout connecting to remote, retrying...');
			this.state['probe'].abort();
		}
		var xhr = new XMLHttpRequest();
		this.state['probe'] = xhr;

		var self = this;
		xhr.open('HEAD', this.url, true);
		xhr.onload = function(evt) {
			delete self.state['probe'];
			switch (evt.target.status) {
				case 204:
					self.setAvail('remote', true, 'Remote accepts OTA');
					break;
				case 403:
					self.setAvail('remote', false, 'Access denied, insufficient permission');
					break;
				case 404:
					self.setAvail('remote', false, 'Bad remote location (check OTA configuration)');
					break;
				case 409:
					self.setAvail('remote', false, 'Another OTA already in progress');
					break;
				default:
					self.setAvail('remote', false, 'Unrecognized status ('+evt.target.status+')');
			}
		};
		xhr.onerror = function(evt) {
			delete self.state['probe'];
			self.setAvail('remote', false, 'Error - ' + (evt.error||'Unable to connect to remote'));
		};
		xhr.onabort = function(evt) {
			delete self.state['probe'];
			self.setAvail('remote', false, 'Connection to remote aborted');
		};
		xhr.send();
	};

	OTA.prototype.selectFile = function(file) {
		if (file) {
			this.state['file'] = file;
			delete this.state['file_md5'];
			this.calcFileHash();
		} else {
			delete this.state['file'];
			delete this.state['file_md5'];
			if (this.state['file_reader']) {
				this.state['file_reader'].abort();
			}
			this.setAvail('file', false, 'No file has been selected');
		}
	};

	OTA.prototype.calcFileHash = function() {
		this.setAvail('file', false, 'Calculating hash for file "'+this.state['file'].name+'"...');
		if (this.state['file_reader']) {
			this.state['file_reader'].abort();
		}
		var reader = new FileReader();
		this.state['file_reader'] = reader;

		var self = this;
		var hasher = md5.create();
		reader.onload = function(evt){
			delete self.state['file_reader'];
			if (self.state['file']) {
				hasher.update(evt.target.result);
				self.state['file_md5'] = hasher.hex();
				self.setAvail('file', true, 'File hash calculated');
			}
		};
		reader.onerror = function(evt){
			delete self.state['file_reader'];
			if (self.state['file']) {
				self.setAvail('file', false, 'Error - ' + (evt.error||'Unable to read the file'));
			}
		};
		reader.onabort = function(evt){
			delete self.state['file_reader'];
			if (self.state['file']) {
				self.setAvail('file', false, 'File hashing aborted');
			}
		};
		reader.readAsArrayBuffer(this.state['file']);
	};

	OTA.prototype.uploadDo = function(prog_cb) {
		if (!this.state.avail) {
			return false;
		}
		this.setAvail('upload', false, 'Upload in progress...');
		this.probeStop();

		var formData = new FormData();
		formData.append('upload', this.state.file);
		var uploadParam = '?length='+this.state.file.size;
		uploadParam += '&md5='+this.state.file_md5;

		var xhr = new XMLHttpRequest();
		this.state['upload'] = xhr;

		var self = this;
		xhr.open('POST', this.url+uploadParam, true);
		xhr.onload = function(evt) {
			delete self.state['upload'];
			self.setAvail('upload', true, 'Upload finished');
			switch (evt.target.status) {
				case 204:
					self.selectFile();
					prog_cb(true);
					return;
				case 400:
					prog_cb(false, 'Request rejected (network error / OTA protocol mis-matched)');
					break;
				case 403:
					prog_cb(false, 'Access denied, insufficient permission');
					break;
				case 404:
					prog_cb(false, 'Bad remote location (check OTA configuration)');
					break;
				case 409:
					prog_cb(false, 'Another OTA already in progress');
					break;
				case 500:
					prog_cb(false, 'Remote operational error - '+(evt.target.responseText||'Generic failure'));
					break;
				default:
					prog_cb(false, 'Unrecognized status ('+evt.target.status+')');
			}
			self.probeStart();
		};
		xhr.onerror = function(evt) {
			delete self.state['upload'];
			self.setAvail('upload', true, 'Upload failed');
			prog_cb(false, 'Error - ' + (evt.error||'Unable to upload to remote'));
			self.probeStart();
		};
		xhr.onabort = function(evt) {
			delete self.state['upload'];
			self.setAvail('upload', true, 'Upload aborted');
			prog_cb(false, 'Upload to remote aborted');
			self.probeStart();
		};
		xhr.upload.onprogress = function(pevt) {
			if (self.state['upload']) {
				if (pevt.lengthComputable) {
					prog_cb(undefined, pevt.loaded/pevt.total);
				} else {
					prog_cb(undefined, pevt.loaded);
				}
			}
		};
		xhr.send(formData);

		return true;
	};

	OTA.prototype.uploadAbort = function() {
		if (this.state['upload']) {
			this.state['upload'].abort();
			delete this.state['upload'];
		}
	};

	window.OTA = OTA;
})();