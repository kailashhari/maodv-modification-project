const nodemailer = require("nodemailer");

const smtp_user = "noreply.deltaforce@gmail.com";
const smtp_pass = "3z@Gq*GmAsb9K$4s";

const mailTransporter = nodemailer.createTransport({
        host: "smtp.gmail.com",
        port: 587,
        ignoreTLS: false,
        secure: false,
        service: "gmail",
        auth: {
                user: smtp_user,
                pass: smtp_pass,
        },
});

const orgOwnersAddProjectsContent = (name) => {
        const html = `<!DOCTYPE html
    PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
<html xmlns="http://www.w3.org/1999/xhtml">

<head>
    <meta http-equiv="Content-Type" content="text/html; charset=UTF-8" />
    <title> title </title>
</head>

<body style="background-color: #e9ecef;">
    <table border="0" cellpadding="0" cellspacing="0" width="100%">
        <tr>
            <td align="center" bgcolor="#e9ecef">
                <table border="0" cellpadding="0" cellspacing="0" width="100%" style="max-width: 600px;">
                    <tr>
                        <td align="center" valign="top" style="padding: 36px 24px;">
                            <a href="https://dwoc.io/">
                                <img src="https://camo.githubusercontent.com/1ba815f1289125317c999a059ec95bfa5cba3894/68747470733a2f2f696d6775722e636f6d2f544c78357273762e706e67"
                                    style="height:75px;width:auto">
                            </a>
                        </td>
                    </tr>
                </table>
            </td>
        </tr>
        <tr>
            <td align="center" bgcolor="#e9ecef">
                <table border="0" cellpadding="0" cellspacing="0" width="100%" style="max-width: 600px;">
                    <tr>
                        <td align="left" bgcolor="#ffffff"
                            style="padding: 36px 24px 0; font-family: 'Source Sans Pro', Helvetica, Arial, sans-serif; border-top: 3px solid #d4dadf;">
                            <h3
                                style="margin: 0; font-size: 25px; font-weight: 700; letter-spacing: 0px; line-height: 48px;">
                                Hi<span style="text-transform:capitalize;"> ${name} </span>,
                            </h3>
                        </td>
                    </tr>
                </table>
            </td>
        </tr>
        <tr>
            <td align="center" bgcolor="#e9ecef">
                <table border="0" cellpadding="0" cellspacing="0" width="100%" style="max-width: 600px;">
                    <tr>
                        <td align="left" bgcolor="#ffffff"
                            style="padding: 24px; font-family: 'Source Sans Pro', Helvetica, Arial, sans-serif; font-size: 16px; line-height: 24px;">
                            <p style="margin: 0;">
                            We are thrilled to inform you that you have been selected as a DWoC mentee to work with OpenGenus on the project ‘Cosmos’. We couldn’t add your name to the poster announcement, but please be assured that on the completion of your coding phase, we will be providing you with a certificate to acknowledge your work.
                            </p>
                            <p>
                            Please feel free to reach out to the respective project mentors and we hope you have an amazing learning experience in the upcoming days.
                            </p>
                            <p>
                                We wish you all the best and happy contributing!
                            </p>
                        </td>
                    </tr>
                </table>
            </td>
        </tr>
    </table>
</body>

</html>`;

        return html;
};

// const emails = require("./mail.json");

// console.log(emails.length);

// for (let i = 0; i < emails.length; i++) {
//         const email = emails[i];
//         // console.log(email)
//         const subject = "Confirmation of project mentees";
//         const mailContent = orgOwnersAddProjectsContent(email.name);
//         const mailDetails = {
//                 from: "Dwoc <no-reply@delta.nitt.edu>",
//                 to: email.email,
//                 subject,
//                 html: mailContent,
//         };
//         mailTransporter.sendMail(mailDetails, (err) => {
//                 if (err) {
//                         console.log(`{"email":"${email.email}","name": "${email.name}"}`);
//                 } else {
//                         console.log(`Email sent successfully to ${email.name}`);
//                 }
//         });
// }

const mailContent = orgOwnersAddProjectsContent("Hariharan");

const mailDetails = {
    from: "Dwoc <no-reply@delta.nitt.edu>",
    // to: "hhmtries@gmail.com",
    // cc: "aditianhacker@gmail.com",
    to: "indreshp135@gmail.com",
    cc: "itsme1327970@gmail.com",
    subject: "Congratulations DWoC mentee!",
    html: mailContent,
};
mailTransporter.sendMail(mailDetails, (err) => {
    if (err) {
        console.log(err)
    } else {
        console.log(`Email sent successfully :)`);
    }
});
