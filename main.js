import { MultiplayerApi } from './MultiplayerApi.js';

//const api = new MultiplayerApi('ws://kontoret.onvo.se/multiplayer');
//const api = new MultiplayerApi('ws://172.19.4.22:8080/multiplayer');
const api = new MultiplayerApi(`ws${location.protocol === 'https:' ? 's' : ''}://${location.host}/multiplayer`);

const canvas = document.getElementById('myCanvas');
const ctx = canvas.getContext('2d');


const hostButton = document.getElementById('hostButton');
const joinButton = document.getElementById('joinButton');
const joinSessionInput = document.getElementById('joinSessionInput');
const joinNameInput = document.getElementById('joinNameInput');
const sendButton = document.getElementById('sendButton');
const listButton = document.getElementById('listButton');
const status = document.getElementById('status');

function initiate() {

	hostButton.addEventListener('click', () => {




		api.host({ session: "Emils chatrum", private: false })
			.then((result) => {
				status.textContent = `Hosted session with ID: ${result.session} with clientId: ${result.clientId}`;
			})
			.catch((error) => {
				console.error('Error hosting session:', error);
			});


		ctx.fillStyle = '#ff0000';
		ctx.fillRect(50, 50, 100, 100);


	});

	joinButton.addEventListener('click', () => {


		api.join(joinSessionInput.value, { name: joinNameInput.value })
			.then((result) => {
				status.textContent = `Joined session: ${result.session} with clientId: ${result.clientId}`;
			})
			.catch((error) => {
				console.error('Error joining session:', error);
			});




	});

	sendButton.addEventListener('click', () => {


		api.game({ msg: "Hello from client!" });

	});

	listButton.addEventListener('click', () => {
		api.list()
			.then((result) => {
				status.textContent = `Active sessions: ${JSON.stringify(result)}`;
			})
			.catch((error) => {
				console.error('Error listing sessions:', error);
			});
	});


	const unsubscribe = api.listen((event, messageId, clientId, data) => {

		status.textContent = `Received event "${event}" with messageId: "${messageId}" from clientId: "${clientId}" and data: ${JSON.stringify(data)}`;

	});

}

window.addEventListener('load', () => {
	initiate();
});
