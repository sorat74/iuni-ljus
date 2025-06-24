const iuni = require('./iuni-sdk')


const client = iuni.connect()

client.get().then(res => {
	console.log(res)
})

client.set("users", "name", "john").then(res => {
	console.log("saved!")
})

client.update("users", "name", "john").with("markus").then(res => {
	console.log("updated!")
})

client.dump().then(res => {
	console.log(res)
})

client.wild(1).set("users", "*", "bookmarks").then(res => {
	
})