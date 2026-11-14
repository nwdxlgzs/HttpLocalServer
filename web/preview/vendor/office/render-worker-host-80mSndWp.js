var e = "" + new URL("./assets/render-worker-Coh33rbW.js", import.meta.url).href;
function t() {
	return new Worker(e, { type: "module" });
}
export { t as createRenderWorker };
