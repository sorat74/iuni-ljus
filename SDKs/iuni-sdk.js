/* author andrea sorato 
   iuni-ljus js sdk
*/

const version = "0.9.1" // 2025.06.01

function connect(selected_db) {

	const net = require('net')
	const PORT = 7212

	function iuni_tcp_encode(s) {
		return s.toString()
			.replaceAll("\\","\\\\")
			.replaceAll("\n","\\n")
	}
	
	function makeRequest (content) {
		return new Promise((resolve, reject) => {
			const client = net.createConnection({ port: PORT, /*highWaterMark: 50384 */})
			
			let data = ""

			client.on('data', (chunk) => {
				data += chunk.toString()
			})
			
			client.on('end', () => {
				resolve(data)
				client.destroy()
			})

			client.on('error', (err) => {
				reject(err)
				client.destroy()
			})
			
			client.write(content)

		})
	}
	
	var wilds = [] // put in env TODO

	function cmd (action, keys) {
		if (!keys) keys = []
		keys = Array.from(keys)
		let body = [
			"USE", selected_db ?? "default",
			action.toUpperCase(),
			...keys
				.map(i => iuni_tcp_encode(i))
				.map((i, index) => wilds?.includes(index) ? i : i.replaceAll("*", "\\*"))
		].join("\n")

		const req = String(body.length).padStart(8, '0') + "\n" + body
		return makeRequest(req)
	}

	/* core */
	

	function get(...keys) {
		return cmd("get", keys).then(res => {
			let arr = res.split("\n").map(i => JSON.parse('"' + i.replaceAll("\"", "\\\"") + '"'))
			return arr
		})
	}
	
	function dump() {
		return cmd("dump", [])
	}

	async function set (...keys) {
		return cmd("set", keys)
	}

	function is (...keys) {
		return cmd("is", keys)
	}

	function drop () {
		return cmd("drop", [])
	}

	function del (...keys) {
		return cmd("del", keys)
	}

	function tree (...keys) {
		return cmd("tree", keys).then(res => {
			// console.log(">>", res)
			
			if (res === "<empty>") return res

			let v = res.split("\n");
			
			let s = ""
			let br = 0
			let j = 0

			for (let i of v) {
				if (i === "{") {
					if (j !== 0) s += ":"
					s += "{"
					br = 1
				}
				else if (i === "}") {
					if (br !== -1) s += ":true"
					s += "}"
					br = -1
				}
				else {
					if (br === -1 && j+1 !== v.length) s += ","
					else if (br === 0) s += ":true,"
					s += '"' + i.replaceAll("\"", "\\\"") + '"'
					br = 0
				}
				j++
			}

			return JSON.parse(s)
		})
	}
	
	function upd (...keys) {
		return ({with: function(...newkeys) {
			return cmd("upd", [
				...keys.map(i => i.replaceAll(":", "\\:")),
				":" , 
				...newkeys.map(i => i.replaceAll(":", "\\:"))
				], wilds)
		}})
	}
	
	/* core end */
	
	const update = upd
	
	function wild(...w) { /* to set which keys have wildcards, indicating keys indexes (e.g.: 0, 1, 2, etc.) */
		wilds = w
		return aclient
	}
	
	const aclient = { get, set, is, del, drop, tree, upd, update, wild, dump }

	return aclient
}

module.exports = {
	connect, version
}
