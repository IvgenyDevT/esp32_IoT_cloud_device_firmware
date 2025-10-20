/**
 * @file script.js
 * @brief Handles Wi-Fi form submission and user feedback.
 *
 * This script sends the Wi-Fi credentials entered by the user
 * to the ESP32 web server using a POST request to "/set".
 * It provides live feedback messages and button animations
 * to make the process clearer for the user.
 *
 * Behavior:
 *  - Prevents default form submission (to avoid page reload).
 *  - Sends form data using Fetch API (POST).
 *  - Displays real-time connection status messages.
 *  - Handles success, error, and button state changes smoothly.
 */

// ---- Select UI elements ----
const form = document.getElementById("wifi-form");
const statusMsg = document.getElementById("status-msg");
const button = document.getElementById("connect-btn");

// ---- Handle form submission ----
form.addEventListener("submit", function (event) {
    // Prevent page reload on form submit
    event.preventDefault();

    // Show user feedback and disable the button temporarily
    statusMsg.textContent = "Connecting...";
    statusMsg.style.color = "orange";
    button.disabled = true;
    button.style.backgroundColor = "#ccc";

    // Collect form data (SSID + password)
    const formData = new FormData(form);

    // Send credentials to ESP32 via HTTP POST
    fetch("/set", {
        method: "POST",
        body: new URLSearchParams(formData),
    })
        .then(response => {
            // If server response is OK → show success message
            if (response.ok) {
                statusMsg.textContent = "Wi-Fi credentials sent! Rebooting...";
                statusMsg.style.color = "green";
            } else {
                // Handle unexpected server response
                throw new Error("Network response not OK");
            }
        })
        .catch(error => {
            // Handle network or server error
            console.error(error);
            statusMsg.textContent = "Error sending credentials!";
            statusMsg.style.color = "red";

            // Re-enable the button so user can retry
            button.disabled = false;
            button.style.backgroundColor = "#0078d7";
        });
});