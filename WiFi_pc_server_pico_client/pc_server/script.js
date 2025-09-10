document.addEventListener('DOMContentLoaded', function() {
    const startButton = document.getElementById('startButton');
    const statusDiv = document.getElementById('status');
    const blinkCountDiv = document.getElementById('blinkCount');

    // Action when the button is clicked
    startButton.addEventListener('click', function() {
        statusDiv.innerText = 'Sending START command to PicoW...';
        fetch('/start-command')
            .then(response => response.json())
            .then(data => {
                statusDiv.innerText = data.message;
            })
            .catch(error => {
                statusDiv.innerText = 'Error sending command.';
            });
    });

    // ★ Function to update the count from the server
    function updateBlinkCount() {
        fetch('/get-count')
            .then(response => response.json())
            .then(data => {
                blinkCountDiv.innerText = data.count;
            })
            .catch(error => {
                console.error('Could not fetch count:', error);
            });
    }

    // ★ Call the update function every 2 seconds (2000 milliseconds)
    setInterval(updateBlinkCount, 2000);
});