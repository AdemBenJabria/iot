const express = require('express');
const mongoose = require('mongoose');
const mqtt = require('mqtt');
const socketio = require('socket.io');
const path = require('path');

const app = express();
const server = require('http').Server(app);
const io = socketio(server);
const uri = "mongodb+srv://Adembj:zvNT2hC6JewvF7Qv@m1miage.0nzctg4.mongodb.net/WaterBnB";

// Connect to MongoDB
mongoose.connect(uri, { useNewUrlParser: true, useUnifiedTopology: true });

// Define the schema and model for users
const userSchema = new mongoose.Schema({
    name: String,
    lat: Number,
    lon: Number,
    batt: Number,
    clientData: [{ id: String, lat: Number, lon: Number, batt: Number, inPool: Boolean, distance: Number }]
});

const poolSchema = new mongoose.Schema({
    name: String,
    lat: Number,
    lon: Number
});

const User = mongoose.model('User', userSchema);
const Pool = mongoose.model('Pool', poolSchema);


// MQTT client
const client = mqtt.connect('mqtt://mqtt.eclipseprojects.io');

let users = []; // Initialize users as an empty array

client.on('connect', function () {
    console.log('Connected to MQTT broker');
    client.subscribe('owntracks/+/+', function (err) {
        if (err) {
            console.error('Failed to subscribe to MQTT topic');
        }
    });
});

client.on('connect', function () {
    console.log('Connected to MQTT pool broker');
    client.subscribe('uca/iot/piscine', function (err) {
        if (err) {
            console.error('Failed to subscribe to MQTT topic');
        }
    });
});

client.on('message', async function (topic, message) {
    console.log('Received message on ' + topic + ': ' + message.toString());

    let payload;
    try {
        payload = JSON.parse(message);
    } catch (error) {
        console.error('Failed to parse MQTT message as JSON:', error);
        return;
    }

    const clientId = topic.split('/')[1]; // Extract the username from the topic

    try {
        users = await User.find({}).exec(); // Update the users variable with the latest data from MongoDB

        const inPool = isClientInPool(payload.lat, payload.lon, users);

        // Update the client data for the corresponding user
        let user = users.find(user => user.name === clientId);
        if (user) {
            if (!user.clientData) {
                user.clientData = []; // Initialize clientData as an empty array if it doesn't exist
            }

            // Remove old client data
            user.clientData = user.clientData.filter(data => data.id !== clientId);

            // Add the client data to the clientData array
            user.clientData.push({ id: clientId, lat: payload.lat, lon: payload.lon, batt: payload.batt, inPool, distance: 0 });

            // Update user coordinates if client is in a pool
            if (inPool) {
                user.lat = payload.lat;
                user.lon = payload.lon;
                await user.save();
            }
        } else {
            console.error(`User ${clientId} not found`);

            const userData = {
                name: clientId,
                lat: payload.lat,
                lon: payload.lon,
                batt: payload.batt,
                clientData: [{ id: clientId, lat: payload.lat, lon: payload.lon, batt: payload.batt, inPool, distance: 0 }]
            };
            user = new User(userData);

            try {
                await user.save();
                console.log(`User ${clientId} added to the database`);
                users.push(user);
            } catch (error) {
                console.error(`Failed to add User ${clientId} to the database`, error);
            }
        }

        io.emit('clientData', user.clientData);

        // Update the dashboard here with the received data
        updateDashboard(users);
    } catch (error) {
        console.error('Failed to fetch users from MongoDB', error);
    }
});

client.on('message', async function (topic, message) {
    console.log('Received message on ' + topic + ': ' + message.toString());

    let payload;
    try {
        payload = JSON.parse(message);
    } catch (error) {
        console.error('Failed to parse MQTT message as JSON:', error);
        return;
    }

    // Vérifier si la piscine existe déjà dans la base de données
    const existingPool = await Pool.findOne({ name: payload.name }).exec();
    if (existingPool) {
        console.log(`Pool ${existingPool.name} already exists in the database. Skipping.`);
        return;
    }

    // Store the pool data in the WaterBnB.pools collection
    const poolData = {
        name: payload.name,
        lat: payload.lat,
        lon: payload.lon
    };

    const pool = new Pool(poolData);

    try {
        await pool.save();
        console.log(`Pool ${pool.name} added to the database`);
    } catch (error) {
        console.error(`Failed to add Pool ${pool.name} to the database`, error);
    }
});


// Function to update the client count and markers for a user
function updateUserData(userName, clientData) {
    const lowerCaseUserName = userName.toLowerCase(); // Convert user name to lowercase
    const userCard = Array.from(userContainer.getElementsByClassName("user-card")).find(card => {
        const headerText = card.querySelector(".card-header").textContent.toLowerCase(); // Convert header text to lowercase
        return headerText === lowerCaseUserName;
    });
    if (userCard) {
        const clientCount = userCard.querySelector(".client-count");
        const clientMarkers = userCard.querySelector(".client-markers");
        const availabilityCount = userCard.querySelector(".availability-count");

        clientCount.textContent = clientData.length;

        clientMarkers.innerHTML = "";
        clientData.forEach(client => {
            const marker = document.createElement("div");
            marker.classList.add("client-marker", client.inPool && client.distance <= 0.1 ? "yellow" : "blue");
            clientMarkers.appendChild(marker);
        });

        const availabilityCountValue = clientData.filter(client => client.inPool && client.distance <= 0.1).length;
        availabilityCount.textContent = availabilityCountValue;
    }
}


// Function to update the dashboard with the received user data
function updateDashboard(users) {
    userContainer.innerHTML = ""; // Clear the userContainer

    // Access the user cards and update them with the new data
    users.forEach(user => {
        createUserCard(user);
        updateUserData(user.name, user.clientData); // Pass the user name and client data to the updateUserData function
    });
}

app.use(express.json());

app.get('/api/users', async (req, res) => {
    try {
        const users = await User.find({});
        res.json(users);
    } catch (error) {
        console.error('Failed to fetch users:', error);
        res.status(500).send('Server error');
    }
});

app.use(express.static(path.join(__dirname, 'public')));

app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'index.html'));
});

const favicon = require('serve-favicon');
app.use(favicon(path.join(__dirname, 'public', 'favicon.ico')));



function isClientInPool(clientLat, clientLon, users) {
    const proximityDistance = 0.1; // Proximity distance in kilometers

    for (const user of users) {
        const distance = haversineDistance(clientLat, clientLon, user.lat, user.lon);
        if (distance <= proximityDistance) {
            return true; // The client is in a pool
        }
    }

    return false; // The client is not in a pool
}

function haversineDistance(lat1, lon1, lat2, lon2) {
    const earthRadius = 6371; // Radius of the Earth in kilometers

    const lat1Rad = degToRad(lat1);
    const lon1Rad = degToRad(lon1);
    const lat2Rad = degToRad(lat2);
    const lon2Rad = degToRad(lon2);

    const deltaLat = lat2Rad - lat1Rad;
    const deltaLon = lon2Rad - lon1Rad;

    const a = Math.sin(deltaLat / 2) ** 2 + Math.cos(lat1Rad) * Math.cos(lat2Rad) * Math.sin(deltaLon / 2) ** 2;
    const c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
    const distance = earthRadius * c;

    return distance;
}

function degToRad(deg) {
    return deg * (Math.PI / 180);
}

const port = process.env.PORT || 3000;
server.listen(port, function () {
    console.log(`Server is running on port ${port}`);
});
