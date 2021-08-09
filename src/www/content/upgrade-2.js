$( function(){                       
   
    var loader = '<div class="d-flex justify-content-center"><div class="spinner-border mt-5" role="status"><span class="sr-only" >Loading...</span></div></div>';
    var alert_msg = '<div class="alert alert-warning alert-dismissible fade show" role="alert">Upgrading ! Do not to disconnect the camera until the process is finished. </div>'
    var reboot_btn = '<button class="btn btn-primary" id="reboot_button">Reboot</button>'
    $.ajax({
        url: 'api/GetFirmwareVersion',
        dataType: 'json',
        success: function (data) {
           
            if(data['errorCode']==0){
                $("#firmware_version").html(data['firmware'])
            }
            
        }
    });
    
    $("#upgrade_button").click(function(event){
        
        event.preventDefault();
        
        // disabled the submit button
        $("#upgrade_button").prop("disabled", true);
        
		console.log("File url : ", $('#file')[0].files[0]);
		if($('#file')[0].files[0] == undefined){
			$("#upgrade_button").prop("disabled", false);
			$('#alert').html('<div class="alert alert-warning" role="alert">Please select firmware file!</div>');
			$('#loading_div').html('');
			return;
		}
		
        $('#loading_div').html(loader);
        $('#alert').html(alert_msg);
        
        var xhr = new XMLHttpRequest();
        
        xhr.onload = function() {
            if (xhr.status == 200)
            {
                console.log("Upload Success.  Rebooting...");
				$("#upgrade_button").prop("disabled", false);
				//$('#alert').html('');
				$('#alert').html('<div class="alert alert-success" role="alert">Upgrade is complete. Rebooting...</div>');
				
				$('#loading_div').html('');
				reboot();
            }
            else
            {
                $("#upgrade_button").prop("disabled", false);
                $('#alert').html('Upload Failed');
				$('#loading_div').html('');
                console.log("Upload Error");
            }
        };
        
        xhr.onerror = function() {
            $("#upgrade_button").prop("disabled", false);
            $('#alert').html('Upload Failed');
			$('#loading_div').html('');
            console.log("Upload error");
        };
        
        xhr.open("POST", "/api/UpgradeFirmware", true);
        xhr.setRequestHeader("Content-Type", "application/octet-stream");
        xhr.send($('#file')[0].files[0]);
    });

    function reboot(){
       var xhr = new XMLHttpRequest();

        xhr.onload = function() {
            if (xhr.status == 200)
            {
                console.log('Reboot Succses');
                $('#alert').html('Upgrade Successful');
            }
            else
            {
                $("#upgrade_button").prop("disabled", false);
                $('#alert').html('Reboot Failed');
				$('#loading_div').html('');
                console.log("Reboot Failed");
            }
        };

        xhr.onerror = function() {
            $("#upgrade_button").prop("disabled", false);
            $('#alert').html('Reboot Failed');
			$('#loading_div').html('');
            console.log("Reboot Failed");
        };

        xhr.open("GET", "/api/Reboot", true);
        xhr.send();
    };

});
