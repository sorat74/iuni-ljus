const iuni = require('./iuni-sdk')


const client = iuni.connect()

async function doWork() {
	let res
	res = await client.set("users") // you can also avoid this
	res = await client.set("users", "john")
	res = await client.set("users", "alex")
	res = await client.set("users", "tom")
	
	res = await client.set("products") // you can also avoid this
	res = await client.set("products", "pencil")
	res = await client.set("products", "rubber")
	res = await client.set("products", "coffee")
}

doWork()
	