class LineChart {
    constructor(canvas, options = {}) {
        this.canvas = canvas;
        this.ctx = canvas.getContext('2d');
        this.dpr = window.devicePixelRatio || 1;
        this.data = [];
        this.maxPoints = options.maxPoints || 300;
        this.lineColor = options.lineColor || '#00aeff';
        this.fillGradientStart = options.fillGradientStart || 'rgba(0, 174, 255, 0.20)';
        this.fillGradientEnd = options.fillGradientEnd || 'rgba(0, 174, 255, 0.01)';
        this.gridColor = options.gridColor || 'rgba(255, 255, 255, 0.04)';
        this.textColor = options.textColor || 'rgba(107, 122, 148, 0.9)';
        this.lineWidth = options.lineWidth || 2;
        this.yMin = options.yMin ?? null;  
        this.yMax = options.yMax ?? null;
        this.yPadding = options.yPadding || 1.15;
        this.thresholds = options.thresholds || [];
        this.padding = { top: 16, right: 16, bottom: 28, left: 48 };
        this._animationFrame = null;
        this._resize();
        this._setupResizeObserver();
    }
    push(value) {
        this.data.push(value);
        if (this.data.length > this.maxPoints) {
            this.data.shift();
        }
    }
    render() {
        const ctx = this.ctx;
        const w = this.canvas.width;
        const h = this.canvas.height;
        ctx.clearRect(0, 0, w, h);
        if (this.data.length < 2) return;
        const dataMin = Math.min(...this.data);
        const dataMax = Math.max(...this.data);
        const yMin = this.yMin ?? Math.max(0, dataMin * 0.8);
        const yMax = this.yMax ?? Math.max(dataMax * this.yPadding, yMin + 1);
        const plotX = this.padding.left * this.dpr;
        const plotY = this.padding.top * this.dpr;
        const plotW = w - (this.padding.left + this.padding.right) * this.dpr;
        const plotH = h - (this.padding.top + this.padding.bottom) * this.dpr;
        const gridLines = this._niceGridLines(yMin, yMax, 5);
        ctx.strokeStyle = this.gridColor;
        ctx.lineWidth = 1;
        ctx.font = `${10 * this.dpr}px Inter, sans-serif`;
        ctx.fillStyle = this.textColor;
        ctx.textAlign = 'right';
        ctx.textBaseline = 'middle';
        for (const gv of gridLines) {
            const gy = plotY + plotH - ((gv - yMin) / (yMax - yMin)) * plotH;
            ctx.beginPath();
            ctx.moveTo(plotX, gy);
            ctx.lineTo(plotX + plotW, gy);
            ctx.stroke();
            ctx.fillText(gv.toFixed(1), plotX - 6 * this.dpr, gy);
        }
        for (const thresh of this.thresholds) {
            if (thresh.value < yMin || thresh.value > yMax) continue;
            const ty = plotY + plotH - ((thresh.value - yMin) / (yMax - yMin)) * plotH;
            ctx.save();
            ctx.strokeStyle = thresh.color || '#ffb800';
            ctx.lineWidth = 1.5 * this.dpr;
            if (thresh.dashed !== false) {
                ctx.setLineDash([6 * this.dpr, 4 * this.dpr]);
            }
            ctx.beginPath();
            ctx.moveTo(plotX, ty);
            ctx.lineTo(plotX + plotW, ty);
            ctx.stroke();
            ctx.restore();
            if (thresh.label) {
                ctx.fillStyle = thresh.color || '#ffb800';
                ctx.font = `${9 * this.dpr}px Inter, sans-serif`;
                ctx.textAlign = 'left';
                ctx.fillText(thresh.label, plotX + 4 * this.dpr, ty - 5 * this.dpr);
            }
        }
        const points = [];
        const step = plotW / (this.maxPoints - 1);
        for (let i = 0; i < this.data.length; i++) {
            const x = plotX + (i + (this.maxPoints - this.data.length)) * step;
            const y = plotY + plotH - ((this.data[i] - yMin) / (yMax - yMin)) * plotH;
            points.push({ x, y });
        }
        if (points.length < 2) return;
        const gradient = ctx.createLinearGradient(0, plotY, 0, plotY + plotH);
        gradient.addColorStop(0, this.fillGradientStart);
        gradient.addColorStop(1, this.fillGradientEnd);
        ctx.beginPath();
        ctx.moveTo(points[0].x, plotY + plotH);
        for (let i = 0; i < points.length; i++) {
            if (i === 0) {
                ctx.lineTo(points[i].x, points[i].y);
            } else {
                const prev = points[i - 1];
                const curr = points[i];
                const cpx = (prev.x + curr.x) / 2;
                ctx.bezierCurveTo(cpx, prev.y, cpx, curr.y, curr.x, curr.y);
            }
        }
        ctx.lineTo(points[points.length - 1].x, plotY + plotH);
        ctx.closePath();
        ctx.fillStyle = gradient;
        ctx.fill();
        ctx.beginPath();
        for (let i = 0; i < points.length; i++) {
            if (i === 0) {
                ctx.moveTo(points[i].x, points[i].y);
            } else {
                const prev = points[i - 1];
                const curr = points[i];
                const cpx = (prev.x + curr.x) / 2;
                ctx.bezierCurveTo(cpx, prev.y, cpx, curr.y, curr.x, curr.y);
            }
        }
        ctx.strokeStyle = this.lineColor;
        ctx.lineWidth = this.lineWidth * this.dpr;
        ctx.lineJoin = 'round';
        ctx.lineCap = 'round';
        ctx.stroke();
        const last = points[points.length - 1];
        ctx.beginPath();
        ctx.arc(last.x, last.y, 3.5 * this.dpr, 0, Math.PI * 2);
        ctx.fillStyle = this.lineColor;
        ctx.fill();
        ctx.strokeStyle = '#0b1022';
        ctx.lineWidth = 2 * this.dpr;
        ctx.stroke();
    }
    _niceGridLines(min, max, count) {
        const range = max - min;
        if (range <= 0) return [min];
        const rawStep = range / count;
        const magnitude = Math.pow(10, Math.floor(Math.log10(rawStep)));
        const normalized = rawStep / magnitude;
        let niceStep;
        if (normalized <= 1) niceStep = magnitude;
        else if (normalized <= 2) niceStep = 2 * magnitude;
        else if (normalized <= 5) niceStep = 5 * magnitude;
        else niceStep = 10 * magnitude;
        const lines = [];
        let v = Math.ceil(min / niceStep) * niceStep;
        while (v <= max) {
            lines.push(v);
            v += niceStep;
        }
        return lines;
    }
    _resize() {
        const rect = this.canvas.parentElement.getBoundingClientRect();
        this.canvas.width = rect.width * this.dpr;
        this.canvas.height = rect.height * this.dpr;
        this.canvas.style.width = rect.width + 'px';
        this.canvas.style.height = rect.height + 'px';
    }
    _setupResizeObserver() {
        if (typeof ResizeObserver !== 'undefined') {
            this._resizeObserver = new ResizeObserver(() => {
                this._resize();
                this.render();
            });
            this._resizeObserver.observe(this.canvas.parentElement);
        }
    }
    destroy() {
        if (this._resizeObserver) {
            this._resizeObserver.disconnect();
        }
        if (this._animationFrame) {
            cancelAnimationFrame(this._animationFrame);
        }
    }
}
window.LineChart = LineChart;
